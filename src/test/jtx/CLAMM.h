#pragma once

#include <test/jtx/Account.h>
#include <test/jtx/Env.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/STIssue.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {
namespace test {
namespace jtx {

// Default sqrt price for tests (tick index 1, ~1:1 price ratio)
inline clamm::uint128
clammDefaultSqrtPrice()
{
    return clamm::tickToSqrtPrice(1);
}

// Compute pool ID from assets and fee tier
inline uint256
clammPoolID(Issue const& asset1, Issue const& asset2, std::uint8_t feeTier)
{
    return keylet::clamm(asset1, asset2, feeTier).key;
}

// Create CLAMMCreate transaction JSON
inline Json::Value
clammCreate(
    jtx::Env& env,
    jtx::Account const& account,
    Issue const& asset,
    Issue const& asset2,
    std::uint8_t feeTier,
    clamm::uint128 const& initialSqrtPrice)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMCreate;
    jv[jss::Account] = account.human();
    jv[jss::Asset] =
        STIssue(sfAsset, asset).getJson(JsonOptions::none);
    jv[jss::Asset2] =
        STIssue(sfAsset2, asset2).getJson(JsonOptions::none);
    jv[sfFeeTier.jsonName] = feeTier;
    jv[sfInitialSqrtPrice.jsonName] =
        to_string(clamm::toSLEField(initialSqrtPrice));
    jv[jss::Fee] = std::to_string(
        env.current()->fees().increment.drops());
    return jv;
}

// Create CLAMMDeposit transaction JSON
inline Json::Value
clammDeposit(
    jtx::Account const& account,
    uint256 const& poolID,
    std::int32_t lowerTick,
    std::int32_t upperTick,
    STAmount const& amount,
    STAmount const& amount2)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMDeposit;
    jv[jss::Account] = account.human();
    jv[sfPoolID.jsonName] = to_string(poolID);
    jv[sfLowerTick.jsonName] = lowerTick;
    jv[sfUpperTick.jsonName] = upperTick;
    amount.setJson(jv[jss::Amount]);
    amount2.setJson(jv[jss::Amount2]);
    return jv;
}

// Create CLAMMSwap transaction JSON
inline Json::Value
clammSwap(
    jtx::Account const& account,
    uint256 const& poolID,
    STAmount const& amountIn)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMSwap;
    jv[jss::Account] = account.human();
    jv[sfPoolID.jsonName] = to_string(poolID);
    amountIn.setJson(jv[jss::Amount]);
    return jv;
}

// Create CLAMMSwap with sqrt price limit
inline Json::Value
clammSwapWithLimit(
    jtx::Account const& account,
    uint256 const& poolID,
    STAmount const& amountIn,
    clamm::uint128 const& sqrtPriceLimit)
{
    auto jv = clammSwap(account, poolID, amountIn);
    jv[sfSqrtPriceLimit.jsonName] =
        to_string(clamm::toSLEField(sqrtPriceLimit));
    return jv;
}

// Create CLAMMWithdraw transaction JSON (full withdraw)
inline Json::Value
clammWithdraw(jtx::Account const& account, uint256 const& nfTokenID)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMWithdraw;
    jv[jss::Account] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nfTokenID);
    return jv;
}

// Create CLAMMWithdraw with specific liquidity amount
inline Json::Value
clammWithdrawPartial(
    jtx::Account const& account,
    uint256 const& nfTokenID,
    clamm::uint128 const& liquidityAmount)
{
    auto jv = clammWithdraw(account, nfTokenID);
    jv[sfLiquidityAmount.jsonName] =
        to_string(clamm::toSLEField(liquidityAmount));
    return jv;
}

// Create CLAMMCollectFees transaction JSON
inline Json::Value
clammCollectFees(jtx::Account const& account, uint256 const& nfTokenID)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMCollectFees;
    jv[jss::Account] = account.human();
    jv[sfNFTokenID.jsonName] = to_string(nfTokenID);
    return jv;
}

// Create CLAMMVote transaction JSON
inline Json::Value
clammVote(
    jtx::Account const& account,
    uint256 const& poolID,
    std::uint16_t tradingFee)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMVote;
    jv[jss::Account] = account.human();
    jv[sfPoolID.jsonName] = to_string(poolID);
    jv[jss::TradingFee] = tradingFee;
    return jv;
}

// Create CLAMMBid transaction JSON
inline Json::Value
clammBid(jtx::Account const& account, uint256 const& poolID)
{
    Json::Value jv;
    jv[jss::TransactionType] = jss::CLAMMBid;
    jv[jss::Account] = account.human();
    jv[sfPoolID.jsonName] = to_string(poolID);
    return jv;
}

// Create CLAMMBid with max bid amount
inline Json::Value
clammBidMax(
    jtx::Account const& account,
    uint256 const& poolID,
    STAmount const& bidMax)
{
    auto jv = clammBid(account, poolID);
    bidMax.setJson(jv[sfBidMax.jsonName]);
    return jv;
}

// Find the NFTokenID for a CLAMMPosition in a pool
inline std::optional<uint256>
clammFindPositionNFT(
    jtx::Env& env,
    jtx::Account const& account,
    uint256 const& poolID)
{
    auto const root = keylet::ownerDir(account.id());
    auto dir = env.current()->read(root);
    if (!dir)
        return std::nullopt;

    for (auto const& item : dir->getFieldV256(sfIndexes))
    {
        auto const sle = env.current()->read(keylet::unchecked(item));
        if (sle && sle->getType() == ltCLAMM_POSITION)
        {
            if (sle->getFieldH256(sfPoolID) == poolID)
                return sle->getFieldH256(sfNFTokenID);
        }
    }
    return std::nullopt;
}

// Setup standard test environment with funded accounts
inline void
clammSetupEnv(
    jtx::Env& env,
    jtx::Account const& gw,
    jtx::Account const& alice,
    jtx::Account const& bob,
    jtx::Account const& carol,
    IOU const& USD)
{
    using namespace jtx;
    env.fund(XRP(100'000), gw, alice, bob, carol);
    env.close();
    env.trust(USD(1'000'000), alice);
    env.trust(USD(1'000'000), bob);
    env.trust(USD(1'000'000), carol);
    env.close();
    env(pay(gw, alice, USD(100'000)));
    env(pay(gw, bob, USD(100'000)));
    env(pay(gw, carol, USD(100'000)));
    env.close();
}

}  // namespace jtx
}  // namespace test
}  // namespace xrpl
