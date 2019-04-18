// #include <eosiolib/asset.hpp>
// #include <eosiolib/eosio.hpp>
#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>
#include <eosio.token.hpp>
#include <string>

using namespace eosio;
using std::string;

/************************* CONTRACT *************************/
CONTRACT sale : public contract
{
  TABLE globalvar
  {
    // dummy, always 0
    uint64_t id;

    // pending token rewards
    uint64_t tokens_left;

    bool running;

    uint64_t primary_key() const
    {
      return id;
    }
  };

  using globalvars_index = eosio::multi_index<"globalvars"_n, globalvar>;

  static constexpr auto eosio_token_n = "eosio.token"_n;
  static constexpr auto eos_symbol = symbol("EOS", 4);
  static constexpr auto transfer_n = "transfer"_n;

  static constexpr auto our_token_n = "sportbetsbet"_n;
  static constexpr auto our_symbol = symbol("SBET", 4);
  static constexpr auto issue_n = "issue"_n;

  globalvars_index _globalvars;

  static constexpr uint64_t max_calc_amount = asset::max_amount / 65535;

  static uint64_t amount_calc(uint64_t amount, uint16_t mul, uint16_t div)
  {
    check(amount < max_calc_amount, "!amount_calc");
    return (amount * mul) / div;
  }

  public:
  // using contract::contract;

  sale(name self, name first_receiver, datastream<const char*> ds)
      : contract(self, first_receiver, ds), _globalvars(self, self.value)
  {
  }

  // system - call once to init global stuff
  ACTION run(bool running)
  {
    require_auth(get_self());

    auto gitr = _globalvars.begin();
    if (gitr == _globalvars.end())
    {
      _globalvars.emplace(get_self(), [&](auto& g) {
        g.id = 0;
        // 100M
        g.tokens_left = 1000000000000;
        g.running = running;
      });
    } else
    {
      if (gitr->running == running)
        return;
      _globalvars.modify(gitr, get_self(), [&](auto g) { g.running = running; });
    }
  }

  static constexpr uint16_t token_per_eos = 5000;

  // handles transfers to issue tokens
  [[eosio::on_notify("eosio.token::transfer")]] void on_transfer(name from, name to, asset quantity,
                                                                 string memo) {
    // we're sending
    if (from == get_self())
      return;
    check(to == get_self(), "!here");
    // check if it's really from eosio.token
    if (get_first_receiver() != eosio_token_n)
      return;

    check(quantity.is_valid(), "Invalid asset");
    if (quantity.symbol != eos_symbol)
    {
      // silently accept everything
      return;
    }

    // check running condition
    const auto gitr = _globalvars.begin();
    check(gitr != _globalvars.end(), "sale not started");
    check(gitr->running, "sale paused");
    check(gitr->tokens_left > 0, "sold out");

    // check amount
    const auto amount = quantity.amount;
    check(amount >= 200ULL * 10000ULL, "Min purchase: 200 EOS");

    auto tokens = amount_calc(amount, token_per_eos, 1);
    if (tokens > gitr->tokens_left)
    {
      const auto token_refund = tokens - gitr->tokens_left;
      const auto eos_refund = amount_calc(token_refund, 1, token_per_eos);
      tokens = amount_calc(amount - eos_refund, token_per_eos, 1);
      if (tokens > gitr->tokens_left)
        tokens = gitr->tokens_left;
      if (eos_refund != 0)
      {
        const auto refund_asset = asset(eos_refund, eos_symbol);
        token::transfer_action send_refund(eosio_token_n, {get_self(), "active"_n});
        send_refund.send(get_self(), from, refund_asset, string("refund"));
      }
    }

    const auto token_asset = asset(tokens, our_symbol);
    token::issue_action issue_action(our_token_n, {our_token_n, "sbetissue"_n});
    issue_action.send(from, token_asset, string("SBET Sale"));

    _globalvars.modify(gitr, get_self(), [&](auto& row) { row.tokens_left -= tokens; });
  }

  // ACTION
  // clearall()
  // {
  //   require_auth(get_self());

  //   auto gitr = _globalvars.begin();
  //   while (gitr != _globalvars.end())
  //   {
  //     gitr = _globalvars.erase(gitr);
  //   }
  // }
};