#include "bank.hpp"
#include <eosio.token.hpp>
#include <eosio/asset.hpp>
#include <eosio/system.hpp>

static constexpr auto eosio_token_n = "eosio.token"_n;
static constexpr auto eos_symbol = symbol("EOS", 4);

static constexpr auto out_token_n = "sportbetsbet"_n;
static constexpr auto our_symbol = symbol("SBET", 4);

static constexpr auto watch_name = "sportbetmonk"_n;
static constexpr auto payroll_name = "sportbetjury"_n;

void bank::initcontract()
{
  // system stuff
  require_auth(get_self());

  auto itr = _stats.begin();
  check(itr == _stats.end(), "Contract is init");

  _stats.emplace(get_self(), [&](auto& s) {
    s.id = 0;
    s.staked = 0;
    s.unstaking = 0;
    s.unclaimed = 0;
  });
}

void bank::stake(name from, asset quantity)
{
  // user auth
  require_auth(from);

  // check symbol
  check(quantity.is_valid(), "invalid quantity");
  const auto amount = quantity.amount;
  check(amount > 0, "must stake positive quantity");
  check(quantity.symbol == our_symbol, "symbol not SBET");

  // check balance
  const auto balance = token::get_balance(out_token_n, from, our_symbol.code()).amount;
  const auto itr = _stakestats.find(from.value);
  const auto has_stakes = itr != _stakestats.end();
  const auto staked = has_stakes ? itr->staked : 0ULL;
  check((staked + amount) <= balance, "overdrawn quantity");

  // stake
  if (has_stakes)
  {
    _stakestats.modify(itr, from, [&](auto& s) { s.staked += amount; });
  } else
  {
    _stakestats.emplace(from, [&](auto& s) {
      s.account = from;
      s.staked = amount;
      s.unstaking = 0;
    });
  }

  // add staked amount to global
  const auto sitr = _stats.begin();
  check(sitr != _stats.end(), "!init");
  _stats.modify(sitr, get_self(), [&](auto& s) { s.staked += amount; });
}

void bank::unstake(name from, asset quantity)
{
  // user auth
  require_auth(from);

  // check symbol
  check(quantity.is_valid(), "invalid quantity");
  const auto amount = quantity.amount;
  check(amount > 0, "must unstake positive quantity");
  check(quantity.symbol == our_symbol, "symbol not SBET");

  // check unstaking
  const auto itr = _stakestats.find(from.value);
  check(itr != _stakestats.end(), "no stake record");
  check(itr->unstaking <= itr->staked, "!unstaking");
  const auto can_unstake = itr->staked - itr->unstaking;
  check(amount <= can_unstake, "overdrawn quantity");

  // add unstaking amount to user
  _stakestats.modify(itr, from, [&](auto& stake) { stake.unstaking += amount; });

  // add unstaking amount to global
  const auto sitr = _stats.begin();
  check(sitr != _stats.end(), "!init");
  check((sitr->unstaking + amount) <= sitr->staked, "!staked");
  _stats.modify(sitr, get_self(), [&](auto& s) { s.unstaking += amount; });

  // add unstaking log record
  unstakings_index unstakings(get_self(), get_self().value);
  const auto uitr = unstakings.find(from.value);
  if (uitr != unstakings.end())
  {
    // modify unstake
    unstakings.modify(uitr, from, [&](auto& u) {
      u.quantity += amount;
      u.timestamp = current_time_point();
    });
  } else
  {
    // add new unstake
    unstakings.emplace(from, [&](auto& u) {
      u.account = from;
      u.quantity = amount;
      u.timestamp = current_time_point();
    });
  }
}

void bank::dounstake()
{
  // system stuff
  require_auth(watch_name);

  // total unstaked amount
  uint64_t unstaked = 0ULL;

  unstakings_index unstakings(get_self(), get_self().value);
  auto uitr = unstakings.begin();

  // allow unstakes made over 24h before
  const auto allow_time = time_point_sec(current_time_point()) - 24U * 3600U;
  while (uitr != unstakings.end())
  {
    if (uitr->timestamp > allow_time)
    {
      // can't unstake
      ++uitr;
      continue;
    }

    const auto quantity = uitr->quantity;
    const auto itr = _stakestats.find(uitr->account.value);
    // check if staked
    check(itr != _stakestats.end(), "!stakestats");
    check(quantity == itr->unstaking, "!unstaking");
    check(quantity <= itr->staked, "!staked");

    if (quantity == itr->staked)
    {
      // remove the whole staked record
      _stakestats.erase(itr);
    } else
    {
      // modify
      _stakestats.modify(itr, name{}, [&](auto& s) {
        s.staked -= quantity;
        s.unstaking -= quantity;
      });
    }

    // calc total
    unstaked += quantity;

    // remove unstakings record and go to the next one
    uitr = unstakings.erase(uitr);
  }

  if (unstaked != 0)
  {
    // remove unstaked amount from global
    const auto sitr = _stats.begin();
    check(sitr != _stats.end(), "!init");
    check(unstaked <= sitr->unstaking, "!unstaking");
    check(unstaked <= sitr->staked, "!staked");
    _stats.modify(sitr, get_self(), [&](auto& s) {
      s.staked -= unstaked;
      s.unstaking -= unstaked;
    });
  }
}

void bank::claim(name from)
{
  // user auth
  require_auth(from);

  // check pending dividends
  dividends_index dividends(get_self(), get_self().value);
  const auto itr = dividends.find(from.value);
  check(itr != dividends.end(), "no dividend record");
  const auto amount = itr->amount;
  check(amount != 0, "no dividends");

  // decrease uncaimed
  const auto sitr = _stats.begin();
  check(sitr != _stats.end(), "!init");
  check(amount <= sitr->unclaimed, "!unclaimed");
  _stats.modify(sitr, get_self(), [&](auto& s) { s.unclaimed -= amount; });

  // pay dividend to the user
  const auto dividend_asset = asset(amount, eos_symbol);
  token::transfer_action pay_action(eosio_token_n, {get_self(), "active"_n});
  pay_action.send(get_self(), from, dividend_asset, "claim dividend");

  // remove dividend record
  dividends.erase(itr);
}

void bank::payroll()
{
  // system stuff
  require_auth(payroll_name);

  const auto sitr = _stats.begin();
  check(sitr != _stats.end(), "!init");
  check(sitr->unstaking <= sitr->staked, "!unstaking");
  const auto total_staked = sitr->staked - sitr->unstaking;

  // check(total_staked > 0, "no tokens staked");
  if (total_staked == 0)
  {
    // no tokens staked
    return;
  }

  // get EOS balance
  const auto full_balance = token::get_balance(eosio_token_n, get_self(), eos_symbol.code()).amount;
  check(sitr->unclaimed <= full_balance, "!balance");
  // balance 128 bit
  const auto balance = static_cast<uint128_t>(full_balance - sitr->unclaimed);
  // check(balance > 0, "no EOS to payroll");
  if (balance == 0)
  {
    // nothing to payroll
    return;
  }

  uint64_t total_dividends = 0;

  dividends_index dividends(get_self(), get_self().value);
  for (auto& stake : _stakestats)
  {
    check(stake.unstaking < stake.staked, "!unstaking.s");
    const auto part = stake.staked - stake.unstaking;
    if (part == 0)
      continue;
    check(part <= total_staked, "!part");
    const auto div_128 = (balance * part) / total_staked;

    if (div_128 == 0)
    {
      // nothing to pay to this poor guy
      continue;
    }

    check(div_128 < asset::max_amount, "!div_128");

    // ok, calc him some EOS
    const auto dividend = static_cast<uint64_t>(div_128);
    total_dividends += dividend;

    const auto ditr = dividends.find(stake.account.value);
    if (ditr != dividends.end())
    {
      // add dividends
      dividends.modify(ditr, get_self(), [&](auto& d) { d.amount += dividend; });
    } else
    {
      dividends.emplace(get_self(), [&](auto& d) {
        d.account = stake.account;
        d.amount = dividend;
      });
    }
  }

  if (total_dividends == 0)
    return;

  // update globals
  check(total_dividends <= balance, "!total_dividends");
  _stats.modify(sitr, get_self(), [&](auto& s) { s.unclaimed += total_dividends; });

  // add history record
  payouts_index payouts(get_self(), get_self().value);

  payouts.emplace(get_self(), [&](auto& p) {
    p.timestamp = current_time_point();
    p.dividends = asset(total_dividends, eos_symbol);
    p.stakes = asset(total_staked, our_symbol);
  });
}