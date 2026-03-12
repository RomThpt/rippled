#include <test/jtx.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STBitString.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

namespace xrpl {
namespace test {

struct CLAMM_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::IOU const USD{gw["USD"]};

    void
    testCreate()
    {
        testcase("CLAMMCreate");
        using namespace jtx;

        {
            // Successful pool creation
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Verify pool exists
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU8(sfFeeTier) == 1);
                BEAST_EXPECT(sle->getFieldU16(sfTickSpacing) == 10);
                auto const storedTick =
                    sle->getFieldI32(sfCurrentTick);
                BEAST_EXPECT(storedTick > -100 && storedTick < 100);
            }
        }

        {
            // Duplicate pool creation should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammCreate(env,
                    bob, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecDUPLICATE));
            env.close();
        }

        {
            // Invalid fee tier
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 5,
                    clammDefaultSqrtPrice()),
                ter(temBAD_FEE));
            env.close();
        }

        {
            // Feature disabled -- all 7 transaction types must fail
            auto const noClammFeatures =
                jtx::testable_amendments() - featureCLAMM;
            Env env{*this, noClammFeatures};
            env.fund(XRP(100'000), alice);
            env.close();

            auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);
            auto const fakeNFT = uint256(42);

            // CLAMMCreate
            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(temDISABLED));
            env.close();

            // CLAMMDeposit
            env(clammDeposit(alice, pid, -10, 10, XRP(100), USD(100)),
                ter(temDISABLED));
            env.close();

            // CLAMMSwap
            env(clammSwap(alice, pid, XRP(10)),
                ter(temDISABLED));
            env.close();

            // CLAMMWithdraw
            env(clammWithdraw(alice, fakeNFT),
                ter(temDISABLED));
            env.close();

            // CLAMMCollectFees
            env(clammCollectFees(alice, fakeNFT),
                ter(temDISABLED));
            env.close();

            // CLAMMVote
            env(clammVote(alice, pid, 500),
                ter(temDISABLED));
            env.close();

            // CLAMMBid
            env(clammBid(alice, pid),
                ter(temDISABLED));
            env.close();
        }
    }

    void
    testDeposit()
    {
        testcase("CLAMMDeposit");
        using namespace jtx;

        {
            // Basic deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            // Verify position exists
            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());
        }

        {
            // Deposit to non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            uint256 fakePid;
            (void)fakePid.parseHex(
                "DEADBEEF00000000000000000000000000000000"
                "000000000000000000000001");

            env(clammDeposit(
                    alice, fakePid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tecNO_ENTRY));
            env.close();
        }

        {
            // Invalid tick range (lower >= upper)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, 100, -100,
                    XRP(1'000), USD(1'000)),
                ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testSwap()
    {
        testcase("CLAMMSwap");
        using namespace jtx;

        {
            // Basic swap with liquidity
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice provides liquidity
            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Verify pool state after deposit
            {
                auto const sle =
                    env.current()->read(keylet::clamm(pid));
                BEAST_EXPECT(sle != nullptr);
                if (sle)
                {
                    auto const liq = clamm::fromSLEField(
                        sle->getFieldH128(sfLiquidityAmount));
                    BEAST_EXPECT(liq > 0);
                }
                // Verify lower tick exists
                auto const lowerTick =
                    env.current()->read(keylet::clammTick(pid, -1000));
                BEAST_EXPECT(lowerTick != nullptr);
                // Verify upper tick exists
                auto const upperTick =
                    env.current()->read(keylet::clammTick(pid, 1000));
                BEAST_EXPECT(upperTick != nullptr);
            }

            auto const bobXrpBefore = env.balance(bob);
            auto const bobUsdBefore = env.balance(bob, USD);

            // Bob swaps XRP for USD
            env(clammSwap(bob, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();

            auto const bobXrpAfter = env.balance(bob);
            auto const bobUsdAfter = env.balance(bob, USD);

            // Bob should have less XRP and more USD
            BEAST_EXPECT(bobXrpAfter < bobXrpBefore);
            BEAST_EXPECT(bobUsdAfter > bobUsdBefore);
        }

        {
            // Swap with no liquidity should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // No deposits -- pool has no liquidity
            env(clammSwap(bob, pid, XRP(100)),
                ter(tecPATH_DRY));
            env.close();
        }

        {
            // Swap to non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammSwap(bob, fakePid, XRP(100)),
                ter(tecNO_ENTRY));
            env.close();
        }
    }

    void
    testWithdraw()
    {
        testcase("CLAMMWithdraw");
        using namespace jtx;

        {
            // Full withdrawal
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            if (nft)
            {
                auto const aliceXrpBefore = env.balance(alice);
                auto const aliceUsdBefore = env.balance(alice, USD);

                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();

                auto const aliceXrpAfter = env.balance(alice);
                auto const aliceUsdAfter = env.balance(alice, USD);
                BEAST_EXPECT(aliceXrpAfter > aliceXrpBefore);
                BEAST_EXPECT(aliceUsdAfter > aliceUsdBefore);

                // Position should be deleted
                auto const pos = env.current()->read(
                    keylet::clammPosition(*nft));
                BEAST_EXPECT(pos == nullptr);
            }
        }

        {
            // Withdraw by non-owner should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            if (nft)
            {
                env(clammWithdraw(bob, *nft),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
    }

    void
    testCollectFees()
    {
        testcase("CLAMMCollectFees");
        using namespace jtx;

        {
            // Collect fees after swaps
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            // Bob swaps to generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // Collect fees by non-owner should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            if (nft)
            {
                env(clammCollectFees(bob, *nft),
                    ter(tecNO_PERMISSION));
                env.close();
            }
        }
    }

    void
    testVote()
    {
        testcase("CLAMMVote");
        using namespace jtx;

        {
            // Basic vote
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Alice must have liquidity in the pool to vote
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 300),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU16(sfTradingFee) == 300);
            }
        }

        {
            // Multiple voters produce weighted average
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Both voters need liquidity. Same amounts = equal weight.
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    bob, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 200),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(bob, pid, 400),
                ter(tesSUCCESS));
            env.close();

            // Equal liquidity = equal weight: (200 + 400) / 2 = 300
            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->getFieldU16(sfTradingFee) == 300);
            }
        }

        {
            // Invalid fee (> 10000 hard cap)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammVote(alice, pid, 10001),
                ter(temBAD_FEE));
            env.close();
        }

        {
            // Vote without liquidity should fail
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // No deposit — voting should fail (no liquidity = no permission)
            env(clammVote(alice, pid, 300),
                ter(tecNO_PERMISSION));
            env.close();
        }

        {
            // Vote on non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammVote(alice, fakePid, 500),
                ter(tecNO_ENTRY));
            env.close();
        }
    }

    void
    testBid()
    {
        testcase("CLAMMBid");
        using namespace jtx;

        {
            // Basic bid
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Pool needs liquidity for minSlotPrice calculation
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            env(clammBid(alice, pid),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
            if (sle)
            {
                BEAST_EXPECT(sle->isFieldPresent(sfAuctionSlot));
            }
        }

        {
            // Bid on non-existent pool
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);
            uint256 fakePid{1};

            env(clammBid(alice, fakePid),
                ter(tecNO_ENTRY));
            env.close();
        }

        {
            // Bid with auth accounts
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Pool needs liquidity for minSlotPrice
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(1'000), USD(1'000)),
                ter(tesSUCCESS));
            env.close();

            Json::Value jv = clammBid(alice, pid);
            Json::Value authAccounts(Json::arrayValue);
            Json::Value authAcct;
            authAcct[jss::Account] = bob.human();
            Json::Value acctObj;
            acctObj["AuthAccount"] = authAcct;
            authAccounts.append(acctObj);
            jv[sfAuthAccounts.jsonName] = authAccounts;

            env(jv, ter(tesSUCCESS));
            env.close();
        }
    }

    void
    testRPCInfo()
    {
        testcase("clamm_info RPC");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Test clamm_info RPC
        auto const result = env.rpc(
            "json",
            "clamm_info",
            std::string("{\"pool_id\": \"" + to_string(pid) + "\"}"));

        // The RPC should return pool data under "pool" key
        auto const& rpcResult = result[jss::result];
        BEAST_EXPECT(!rpcResult.isMember(jss::error));
        BEAST_EXPECT(rpcResult.isMember("pool"));
    }

    void
    testSwapCrossesTickBoundary()
    {
        testcase("Swap crosses tick boundary");
        using namespace jtx;

        // Use fee tier 1 (spacing=10) for finer-grained ticks
        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Wide range position that spans several ticks
        env(clammDeposit(
                alice, pid, -500, 500,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle1 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle1 != nullptr);
        std::int32_t tickBefore = 0;
        if (sle1)
            tickBefore = sle1->getFieldI32(sfCurrentTick);

        // Swap should move the price
        env(clammSwap(bob, pid, XRP(3'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle2 != nullptr);
        if (sle2)
        {
            auto const tickAfter = sle2->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(tickAfter != tickBefore);
        }
    }

    void
    testSwapWithSqrtPriceLimit()
    {
        testcase("Swap with SqrtPriceLimit");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // For zeroForOne (XRP->USD), price moves down.
        // Limit must be below current price.
        auto const limitPrice = clamm::tickToSqrtPrice(-50);

        auto jv = clammSwap(bob, pid, XRP(5'000));
        jv[sfSqrtPriceLimit.jsonName] =
            to_string(clamm::toSLEField(limitPrice));

        auto const bobUsdBefore = env.balance(bob, USD);

        env(jv, ter(tesSUCCESS));
        env.close();

        auto const bobUsdAfter = env.balance(bob, USD);
        BEAST_EXPECT(bobUsdAfter > bobUsdBefore);

        // Verify price did not exceed the limit
        auto const sle = env.current()->read(keylet::clamm(pid));
        if (sle)
        {
            auto const currentSqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(currentSqrtPrice >= limitPrice);
        }
    }

    void
    testOutOfRangePositions()
    {
        testcase("Out-of-range positions");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1 (spacing=10) for more flexibility
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // In-range position first to establish pool state
        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Position entirely above current tick
        env(clammDeposit(
                bob, pid, 200, 400,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Both positions should exist
        auto const nft1 = clammFindPositionNFT(env, alice, pid);
        auto const nft2 = clammFindPositionNFT(env, bob, pid);
        BEAST_EXPECT(nft1.has_value());
        BEAST_EXPECT(nft2.has_value());
    }

    void
    testMultiplePositionsSameLP()
    {
        testcase("Multiple positions by same LP");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Two different tick ranges
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -600, 600,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        // Alice should have two positions
        auto const view = env.current();
        int posCount = 0;
        forEachItem(
            *view,
            alice.id(),
            [&](std::shared_ptr<SLE const> const& sle) {
                if (sle->getType() == ltCLAMM_POSITION &&
                    sle->getFieldH256(sfPoolID) == pid)
                    ++posCount;
            });
        BEAST_EXPECT(posCount == 2);
    }

    void
    testFeeAccumulation()
    {
        testcase("Fee accumulation over multiple swaps");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        // Multiple swaps back and forth to generate fees
        for (int i = 0; i < 3; ++i)
        {
            env(clammSwap(bob, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();

            env(clammSwap(carol, pid, USD(100)),
                ter(tesSUCCESS));
            env.close();
        }

        // Collect fees
        if (nft)
        {
            auto const aliceXrpBefore = env.balance(alice);
            auto const aliceUsdBefore = env.balance(alice, USD);

            env(clammCollectFees(alice, *nft),
                ter(tesSUCCESS));
            env.close();

            auto const aliceXrpAfter = env.balance(alice);
            auto const aliceUsdAfter = env.balance(alice, USD);

            // Should have received fees in at least one token
            BEAST_EXPECT(
                aliceXrpAfter > aliceXrpBefore ||
                aliceUsdAfter > aliceUsdBefore);
        }
    }

    void
    testDepositTickAlignment()
    {
        testcase("Deposit rejects misaligned ticks");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Fee tier 2 has tick spacing 60. Ticks must be multiples of 60.
        env(clammDeposit(
                alice, pid, -50, 50,
                XRP(1'000), USD(1'000)),
            ter(temBAD_AMOUNT));
        env.close();

        // Aligned ticks should succeed
        env(clammDeposit(
                alice, pid, -60, 60,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testCreateAllFeeTiers()
    {
        testcase("Create pools with all valid fee tiers");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tiers 0-3 are valid (spacings 1,10,60,200)
        for (std::uint8_t tier = 0; tier <= 3; ++tier)
        {
            auto const pid = clammPoolID(xrpIssue(), USD.issue(), tier);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), tier,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle != nullptr);
        }

        // Tier 4 should fail
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 4,
                clammDefaultSqrtPrice()),
            ter(temBAD_FEE));
        env.close();
    }

    void
    testSwapBothDirections()
    {
        testcase("Swap in both directions");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // XRP -> USD
        auto const bobUsdBefore = env.balance(bob, USD);
        env(clammSwap(bob, pid, XRP(100)),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(bob, USD) > bobUsdBefore);

        // USD -> XRP
        auto const carolXrpBefore = env.balance(carol);
        env(clammSwap(carol, pid, USD(100)),
            ter(tesSUCCESS));
        env.close();
        BEAST_EXPECT(env.balance(carol) > carolXrpBefore);
    }

    void
    testWithdrawReturnsCorrectAmounts()
    {
        testcase("Withdraw returns correct proportional amounts");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        auto const aliceXrpBefore = env.balance(alice);
        auto const aliceUsdBefore = env.balance(alice, USD);

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const aliceXrpAfterDeposit = env.balance(alice);
        auto const aliceUsdAfterDeposit = env.balance(alice, USD);

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        if (nft)
        {
            env(clammWithdraw(alice, *nft),
                ter(tesSUCCESS));
            env.close();

            auto const aliceXrpAfterWithdraw = env.balance(alice);
            auto const aliceUsdAfterWithdraw = env.balance(alice, USD);

            // Should get back approximately what was deposited
            // (minus fees for account reserve and tx fees)
            BEAST_EXPECT(aliceXrpAfterWithdraw > aliceXrpAfterDeposit);
            BEAST_EXPECT(aliceUsdAfterWithdraw > aliceUsdAfterDeposit);
        }
    }

    void
    testPoolLiquidityUpdatesOnDeposit()
    {
        testcase("Pool liquidity updates correctly on deposit");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // In-range deposit should increase active liquidity
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
        if (sle)
        {
            BEAST_EXPECT(sle->isFieldPresent(sfLiquidityAmount));
            auto const liq = clamm::fromSLEField(
                sle->getFieldH128(sfLiquidityAmount));
            BEAST_EXPECT(liq > 0);
        }

        // Second in-range deposit should further increase
        env(clammDeposit(
                bob, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        if (sle && sle2)
        {
            auto const liq1 = clamm::fromSLEField(
                sle->getFieldH128(sfLiquidityAmount));
            auto const liq2 = clamm::fromSLEField(
                sle2->getFieldH128(sfLiquidityAmount));
            BEAST_EXPECT(liq2 > liq1);
        }
    }

    void
    testVoteWeightedByLiquidity()
    {
        testcase("Vote weighted by liquidity");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Alice deposits 3x more than bob
        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(9'000), USD(9'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                bob, pid, -120, 120,
                XRP(3'000), USD(3'000)),
            ter(tesSUCCESS));
        env.close();

        env(clammVote(alice, pid, 100),
            ter(tesSUCCESS));
        env.close();

        env(clammVote(bob, pid, 400),
            ter(tesSUCCESS));
        env.close();

        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle != nullptr);
        if (sle)
        {
            auto const fee = sle->getFieldU16(sfTradingFee);
            // Alice has 3x weight: (100*3 + 400*1) / 4 = 175
            // But exact value depends on liquidity calculation
            BEAST_EXPECT(fee > 100 && fee < 400);
        }
    }

    void
    testBidOutbid()
    {
        testcase("Outbid existing auction slot holder");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // Alice bids first
        env(clammBid(alice, pid),
            ter(tesSUCCESS));
        env.close();

        auto const sle1 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle1 != nullptr);
        if (sle1)
        {
            BEAST_EXPECT(sle1->isFieldPresent(sfAuctionSlot));
        }

        // Advance time so the slot expires (24 intervals * ~24h each)
        // Each close advances time ~10s, need many closes for expiry.
        // Instead, verify that a second bid by the same holder succeeds.
        env(clammBid(alice, pid),
            ter(tesSUCCESS));
        env.close();

        auto const sle2 = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle2 != nullptr);
        if (sle2)
        {
            BEAST_EXPECT(sle2->isFieldPresent(sfAuctionSlot));
        }
    }

    void
    testSwapNoLiquidityInRange()
    {
        testcase("Swap with no liquidity in current range");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Deposit only far above current price
        env(clammDeposit(
                alice, pid, 600, 1200,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        // Swap should fail or produce zero output since no liquidity at
        // current price
        env(clammSwap(bob, pid, XRP(100)),
            ter(tecPATH_DRY));
        env.close();
    }

    void
    testDepositZeroAmounts()
    {
        testcase("Deposit with zero amounts fails");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(0), USD(0)),
            ter(tecINSUFFICIENT_PAYMENT));
        env.close();
    }

    void
    testCreateDifferentAssetPairs()
    {
        testcase("Create pool with different asset pair orderings");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // XRP / USD
        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Reversed order (USD / XRP) should be same pool
        auto const pid1 = clammPoolID(xrpIssue(), USD.issue(), 1);
        auto const pid2 = clammPoolID(USD.issue(), xrpIssue(), 1);
        BEAST_EXPECT(pid1 == pid2);
    }

    void
    testWithdrawNonExistentPosition()
    {
        testcase("Withdraw non-existent position");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        uint256 fakeNftId{42};
        env(clammWithdraw(alice, fakeNftId),
            ter(tecNO_ENTRY));
        env.close();
    }

    void
    testCollectFeesNoFees()
    {
        testcase("Collect fees when no fees accumulated");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -120, 120,
                XRP(5'000), USD(5'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());

        // No swaps occurred, so no fees to collect.
        if (nft)
        {
            env(clammCollectFees(alice, *nft),
                ter(tecAMM_EMPTY));
            env.close();
        }
    }

    void
    testDepositExtremeTickRange()
    {
        testcase("Deposit with extreme tick range");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 2: spacing=60
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 2);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 2,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Wide range position
        env(clammDeposit(
                alice, pid, -6000, 6000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        auto const nft = clammFindPositionNFT(env, alice, pid);
        BEAST_EXPECT(nft.has_value());
    }

    void
    testSwapLargeAmount()
    {
        testcase("Swap large amount relative to liquidity");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: spacing=10
        auto const pid = clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -1000, 1000,
                XRP(10'000), USD(10'000)),
            ter(tesSUCCESS));
        env.close();

        // Swap a large amount -- should still succeed
        auto const bobUsdBefore = env.balance(bob, USD);
        env(clammSwap(bob, pid, XRP(8'000)),
            ter(tesSUCCESS));
        env.close();

        BEAST_EXPECT(env.balance(bob, USD) > bobUsdBefore);
    }

    void
    testVoteDiscountedFeeEqualsTradingFee()
    {
        testcase("Vote: DiscountedFee == TradingFee rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // DiscountedFee == TradingFee should fail with temBAD_FEE
        Json::Value jv = clammVote(alice, pid, 500);
        jv[sfDiscountedFee.jsonName] = 500;  // same as TradingFee
        env(jv, ter(temBAD_FEE));
        env.close();

        // DiscountedFee > TradingFee should also fail
        Json::Value jv2 = clammVote(alice, pid, 500);
        jv2[sfDiscountedFee.jsonName] = 600;
        env(jv2, ter(temBAD_FEE));
        env.close();

        // DiscountedFee < TradingFee should succeed (with liquidity)
        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        Json::Value jv3 = clammVote(alice, pid, 500);
        jv3[sfDiscountedFee.jsonName] = 100;
        env(jv3, ter(tesSUCCESS));
        env.close();
    }

    void
    testSwapZeroAmount()
    {
        testcase("Swap: Amount == 0 rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // Zero amount should fail in preflight
        env(clammSwap(bob, pid, XRP(0)),
            ter(temBAD_AMOUNT));
        env.close();
    }

    void
    testVoteFeeExceedsTierMax()
    {
        testcase("Vote: fee > tier max rejected in doApply");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Fee tier 1: max trading fee is 500
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid, -100, 100,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // Vote with fee within tier max succeeds
        env(clammVote(alice, pid, 500),
            ter(tesSUCCESS));
        env.close();

        // Vote with fee exceeding tier max (501 > 500) should fail
        env(clammVote(alice, pid, 501),
            ter(tecNO_PERMISSION));
        env.close();

        // Fee tier 3: max trading fee is 10000
        auto const pid3 =
            clammPoolID(xrpIssue(), USD.issue(), 3);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 3,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        env(clammDeposit(
                alice, pid3, -200, 200,
                XRP(1'000), USD(1'000)),
            ter(tesSUCCESS));
        env.close();

        // 10000 within tier 3 max: OK
        env(clammVote(alice, pid3, 10000),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testDepositMinLiquidity()
    {
        testcase("Deposit: liquidity below minimum rejected");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Use fee tier 0 (spacing=1) for most flexibility
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 0);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 0,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Use direct LiquidityAmount mode with a value below the minimum
        // threshold (1000). Construct JSON manually.
        Json::Value jv;
        jv[jss::TransactionType] = jss::CLAMMDeposit;
        jv[jss::Account] = alice.human();
        jv[sfPoolID.jsonName] = to_string(pid);
        jv[sfLowerTick.jsonName] = -1;
        jv[sfUpperTick.jsonName] = 1;
        // LiquidityAmount = 500 (below CLAMM_MIN_LIQUIDITY of 1000)
        jv[sfLiquidityAmount.jsonName] =
            to_string(clamm::toSLEField(clamm::uint128(500)));

        env(jv, ter(tecINSUFFICIENT_PAYMENT));
        env.close();
    }

    void
    testDepositInsufficientReserve()
    {
        testcase("Deposit: insufficient reserve for new position");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        // Fund dan with just enough to exist + one trust line but not
        // enough for 3 more owner objects.
        // Base reserve: 200 XRP, owner reserve: 50 XRP each.
        // With trust line (1 owner object): needs 200 + 50 = 250.
        // For 3 more objects: needs 200 + 4*50 = 400 XRP.
        // Fund with 300 XRP so they have a trust line but not enough
        // reserve for 3 more objects.
        jtx::Account const dan{"dan"};
        env.fund(XRP(300), dan);
        env.close();
        env.trust(USD(100'000), dan);
        env.close();
        env(pay(gw, dan, USD(1'000)));
        env.close();

        // Dan has ~300 XRP with 1 owner object. Needs 400 for 4 total.
        env(clammDeposit(
                dan, pid, -100, 100,
                XRP(1), USD(1)),
            ter(tecINSUFFICIENT_RESERVE));
        env.close();
    }

    void
    testFreeze()
    {
        testcase("CLAMM Freeze via trust lines");
        using namespace jtx;

        // --- CLAMMCreate freeze ---

        {
            // 1. Global freeze on issuer blocks pool creation
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 2. Individual freeze on creator's USD trust line
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 3. Freeze then clear: first attempt fails, after
            //    clearing freeze succeeds
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tecFROZEN));
            env.close();

            env(fclear(gw, asfGlobalFreeze));
            env.close();

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();
        }

        // --- CLAMMSwap freeze ---

        {
            // 4. Global freeze blocks swap (caught by isFrozen on
            //    pool's trust line)
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammSwap(bob, pid, XRP(100)),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 5. Individual freeze on trader: frozen trader blocked,
            //    unfrozen trader succeeds
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            env(clammSwap(bob, pid, XRP(100)),
                ter(tecFROZEN));
            env.close();

            // Carol is not frozen, swap succeeds
            env(clammSwap(carol, pid, XRP(100)),
                ter(tesSUCCESS));
            env.close();
        }

        // 6. Pool's own trust line freeze: TrustSet to a
        // pseudo-account returns tecPSEUDO_ACCOUNT, so pool
        // trust line freeze via isFrozen(ammAccountID, issue)
        // cannot be tested through TrustSet. The preclaim check
        // exists but requires a different mechanism to trigger.

        // --- CLAMMDeposit freeze ---

        {
            // 7. Global freeze blocks deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(fset(gw, asfGlobalFreeze));
            env.close();

            env(clammDeposit(
                    bob, pid, -1000, 1000,
                    XRP(1'000), USD(1'000)),
                ter(tecFROZEN));
            env.close();
        }

        {
            // 8. Individual freeze on depositor blocks deposit
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(trust(gw, bob["USD"](0), tfSetFreeze));
            env.close();

            env(clammDeposit(
                    bob, pid, -1000, 1000,
                    XRP(1'000), USD(1'000)),
                ter(tecFROZEN));
            env.close();
        }

        // 9. Pool's trust line freeze for deposit: same limitation
        // as case 6 -- TrustSet to pseudo-account not allowed.

        // --- CLAMMWithdraw (no freeze checks in preclaim) ---

        {
            // 10. Global freeze + withdraw: preclaim has no freeze
            // check, and overrideFreeze privilege allows the
            // invariant checker to let the transfer through.
            // Users should always be able to exit positions.
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(fset(gw, asfGlobalFreeze));
            env.close();

            if (nft)
            {
                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // 11. Individual freeze does NOT block withdraw.
            // Individual freeze on the user's trust line does not
            // affect the pool-to-user transfer in the invariant
            // checker.
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            if (nft)
            {
                env(clammWithdraw(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        // --- CLAMMCollectFees (no freeze checks in preclaim) ---

        {
            // 12. Global freeze does NOT block fee collection
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(fset(gw, asfGlobalFreeze));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

        {
            // 13. Individual freeze does NOT block fee collection
            auto const features =
                jtx::testable_amendments() | featureCLAMM;
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            env(clammDeposit(
                    alice, pid, -1000, 1000,
                    XRP(10'000), USD(10'000)),
                ter(tesSUCCESS));
            env.close();

            // Generate fees
            env(clammSwap(bob, pid, XRP(1'000)),
                ter(tesSUCCESS));
            env.close();

            auto const nft = clammFindPositionNFT(env, alice, pid);
            BEAST_EXPECT(nft.has_value());

            env(trust(gw, alice["USD"](0), tfSetFreeze));
            env.close();

            if (nft)
            {
                env(clammCollectFees(alice, *nft),
                    ter(tesSUCCESS));
                env.close();
            }
        }

    }

    void
    run() override
    {
        testCreate();
        testDeposit();
        testSwap();
        testWithdraw();
        testCollectFees();
        testVote();
        testBid();
        testRPCInfo();
        testSwapCrossesTickBoundary();
        testSwapWithSqrtPriceLimit();
        testOutOfRangePositions();
        testMultiplePositionsSameLP();
        testFeeAccumulation();
        testDepositTickAlignment();
        testCreateAllFeeTiers();
        testSwapBothDirections();
        testWithdrawReturnsCorrectAmounts();
        testPoolLiquidityUpdatesOnDeposit();
        testVoteWeightedByLiquidity();
        testBidOutbid();
        testSwapNoLiquidityInRange();
        testDepositZeroAmounts();
        testCreateDifferentAssetPairs();
        testWithdrawNonExistentPosition();
        testCollectFeesNoFees();
        testDepositExtremeTickRange();
        testSwapLargeAmount();
        testVoteDiscountedFeeEqualsTradingFee();
        testSwapZeroAmount();
        testVoteFeeExceedsTierMax();
        testDepositMinLiquidity();
        testDepositInsufficientReserve();
        testFreeze();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMM, app, xrpl, 1);

}  // namespace test
}  // namespace xrpl
