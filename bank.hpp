#pragma once

#include <eosio/eosio.hpp>
#include <eosio/asset.hpp>

#include <string>

using namespace eosio;
using std::string;

CONTRACT bank : public contract
{
  TABLE stat
  {
    uint64_t id;
    uint64_t staked;
    uint64_t unstaking;
    uint64_t unclaimed;

    uint64_t primary_key() const
    {
      return id;
    }
  };
  using stats_index = multi_index<"stats"_n, stat>;

  TABLE stakestat
  {
    name account;
    uint64_t staked;
    uint64_t unstaking;

    uint64_t primary_key() const
    {
      return account.value;
    }
  };
  using stakestats_index = multi_index<"stakestats"_n, stakestat>;

  TABLE unstaking
  {
    name account;
    uint64_t quantity;
    time_point_sec timestamp;
    uint64_t primary_key() const
    {
      return account.value;
    }
  };
  using unstakings_index = multi_index<"unstakings"_n, unstaking>;

  TABLE dividend
  {
    name account;
    uint64_t amount;

    uint64_t primary_key() const
    {
      return account.value;
    }
  };
  using dividends_index = multi_index<"dividends"_n, dividend>;

  TABLE payout
  {
    time_point timestamp;
    asset stakes;
    asset dividends;

    uint32_t primary_key() const
    {
      return timestamp.elapsed.count();
    }
  };
  using payouts_index = multi_index<"payouts"_n, payout>;

  stats_index _stats;
  stakestats_index _stakestats;

  public:
  bank(name self, name first_receiver, datastream<const char*> ds)
      : contract(self, first_receiver, ds), _stats(self, self.value), _stakestats(self, self.value)
  {
  }

  ACTION initcontract();
  ACTION stake(name from, asset quantity);
  ACTION unstake(name from, asset quantity);
  ACTION dounstake();
  ACTION claim(name from);
  ACTION payroll();

  static uint64_t get_staked(name stake_contract_account, name owner)
  {
    stakestats_index stakestats(stake_contract_account, stake_contract_account.value);
    const auto itr = stakestats.find(owner.value);
    return (itr != stakestats.end()) ? itr->staked : 0;
  }

  // static asset get_staked(name token_contract_account, name owner, symbol_code sym_code)
  // {
  //   accounts accountstable(token_contract_account, owner.value);
  //   const auto& ac = accountstable.get(sym_code.raw());
  //   return ac.balance;
  // }
};