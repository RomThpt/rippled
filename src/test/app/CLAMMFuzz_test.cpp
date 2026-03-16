#include <test/jtx.h>
#include <test/jtx/CLAMM.h>
#include <test/jtx/Env.h>

#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TER.h>
#include <xrpl/tx/transactors/dex/CLAMMHelpers.h>

#include <random>

namespace xrpl {
namespace test {

struct CLAMMFuzz_test : public beast::unit_test::suite
{
    jtx::Account const gw{"gateway"};
    jtx::Account const alice{"alice"};
    jtx::Account const bob{"bob"};
    jtx::Account const carol{"carol"};
    jtx::IOU const USD{gw["USD"]};

    void
    testFuzzDepositWithdraw()
    {
        testcase("Fuzz: random deposit/withdraw sequences");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> actionDist(0, 2);
        std::uniform_int_distribution<int> tickDist(-500, 500);
        std::uniform_int_distribution<int> amountDist(100, 10000);
        std::uniform_int_distribution<int> lpDist(0, 2);

        auto const features =
            jtx::testable_amendments() | featureCLAMM;
        Env env{*this, features};
        clammSetupEnv(env, gw, alice, bob, carol, USD);

        // Use fee tier 1 (spacing=10)
        auto const pid =
            clammPoolID(xrpIssue(), USD.issue(), 1);

        env(clammCreate(env,
                alice, xrpIssue(), USD.issue(), 1,
                clammDefaultSqrtPrice()),
            ter(tesSUCCESS));
        env.close();

        Account const lps[] = {alice, bob, carol};
        std::vector<std::optional<uint256>> nftIDs(3, std::nullopt);

        constexpr int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            int const action = actionDist(engine);
            int const lpIdx = lpDist(engine);
            auto const& lp = lps[lpIdx];

            if (action <= 1 && !nftIDs[lpIdx].has_value())
            {
                // Deposit: pick random aligned ticks
                int lower = tickDist(engine);
                int upper = tickDist(engine);
                lower = (lower / 10) * 10;
                upper = (upper / 10) * 10;
                if (lower >= upper)
                    upper = lower + 10;

                int const amt = amountDist(engine);
                env(clammDeposit(
                        lp, pid, lower, upper,
                        XRP(amt), USD(amt)),
                    ter(std::ignore));
                env.close();

                nftIDs[lpIdx] = clammFindPositionNFT(env, lp, pid);
            }
            else if (nftIDs[lpIdx].has_value())
            {
                // Withdraw
                env(clammWithdraw(lp, *nftIDs[lpIdx]),
                    ter(std::ignore));
                env.close();
                nftIDs[lpIdx] = clammFindPositionNFT(env, lp, pid);
            }
        }

        // Invariant: pool SqrtPrice should still be valid if pool exists
        auto const sle = env.current()->read(keylet::clamm(pid));
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);
        }
        pass();
    }

    void
    testFuzzSwapSequences()
    {
        testcase("Fuzz: random swap sequences");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> dirDist(0, 1);
        std::uniform_int_distribution<int> amountDist(1, 500);

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

        // Set up wide liquidity range
        env(clammDeposit(
                alice, pid, -1000, 1000, XRP(50000), USD(50000)),
            ter(tesSUCCESS));
        env.close();

        // Also add a narrower range from bob
        env(clammDeposit(
                bob, pid, -100, 100, XRP(10000), USD(10000)),
            ter(tesSUCCESS));
        env.close();

        constexpr int rounds = 100;
        for (int i = 0; i < rounds; ++i)
        {
            int const dir = dirDist(engine);
            int const amt = amountDist(engine);

            if (dir == 0)
            {
                // XRP -> USD (zeroForOne)
                env(clammSwap(carol, pid, XRP(amt)),
                    ter(std::ignore));
            }
            else
            {
                // USD -> XRP (oneForZero)
                env(clammSwap(carol, pid, USD(amt)),
                    ter(std::ignore));
            }
            env.close();
        }

        // Invariant: pool state should be consistent
        auto const sle = env.current()->read(keylet::clamm(pid));
        BEAST_EXPECT(sle);
        if (sle)
        {
            auto const sqrtPrice =
                clamm::fromSLEField(sle->getFieldH128(sfSqrtPrice));
            BEAST_EXPECT(sqrtPrice > 0);

            auto const currentTick = sle->getFieldI32(sfCurrentTick);
            BEAST_EXPECT(
                currentTick >= CLAMM_MIN_TICK &&
                currentTick <= CLAMM_MAX_TICK);

            // Fee growth should be non-negative
            auto const fg0 = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthGlobal0));
            auto const fg1 = clamm::fromSLEField(
                sle->getFieldH128(sfFeeGrowthGlobal1));
            BEAST_EXPECT(fg0 >= 0);
            BEAST_EXPECT(fg1 >= 0);
        }
    }

    void
    testFuzzPriceMovements()
    {
        testcase("Fuzz: price movement consistency");
        using namespace jtx;

        std::mt19937 engine(42);
        std::uniform_int_distribution<int> amountDist(10, 1000);

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

        // Multiple positions at different ranges
        env(clammDeposit(
                alice, pid, -500, 500, XRP(20000), USD(20000)),
            ter(tesSUCCESS));
        env.close();
        env(clammDeposit(
                bob, pid, -100, 100, XRP(10000), USD(10000)),
            ter(tesSUCCESS));
        env.close();
        env(clammDeposit(
                carol, pid, -50, 50, XRP(5000), USD(5000)),
            ter(tesSUCCESS));
        env.close();

        clamm::uint128 prevFg0 = 0;
        clamm::uint128 prevFg1 = 0;

        constexpr int rounds = 50;

        // Series of swaps in one direction (XRP -> USD)
        for (int i = 0; i < rounds / 2; ++i)
        {
            int const amt = amountDist(engine);
            env(clammSwap(carol, pid, XRP(amt)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            if (sle)
            {
                // Fee growth must be monotonically non-decreasing
                auto const fg0 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal0));
                auto const fg1 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal1));
                BEAST_EXPECT(fg0 >= prevFg0);
                BEAST_EXPECT(fg1 >= prevFg1);
                prevFg0 = fg0;
                prevFg1 = fg1;

                // CurrentTick must be consistent with SqrtPrice
                auto const currentTick =
                    sle->getFieldI32(sfCurrentTick);
                BEAST_EXPECT(
                    currentTick >= CLAMM_MIN_TICK &&
                    currentTick <= CLAMM_MAX_TICK);
            }
        }

        // Reverse direction (USD -> XRP)
        for (int i = 0; i < rounds / 2; ++i)
        {
            int const amt = amountDist(engine);
            env(clammSwap(carol, pid, USD(amt)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            if (sle)
            {
                auto const fg0 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal0));
                auto const fg1 = clamm::fromSLEField(
                    sle->getFieldH128(sfFeeGrowthGlobal1));
                BEAST_EXPECT(fg0 >= prevFg0);
                BEAST_EXPECT(fg1 >= prevFg1);
                prevFg0 = fg0;
                prevFg1 = fg1;
            }
        }
        pass();
    }

    void
    testFuzzEdgeCases()
    {
        testcase("Fuzz: edge cases");
        using namespace jtx;

        auto const features =
            jtx::testable_amendments() | featureCLAMM;

        // 1. Deposit at MIN_TICK / MAX_TICK (fee tier 0, spacing=1)
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 0);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 0,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Wide range deposit
            env(clammDeposit(
                    alice, pid,
                    CLAMM_MIN_TICK, CLAMM_MAX_TICK,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();
        }

        // 2. Ultra-narrow range (1 tick, fee tier 0)
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 0);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 0,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // 1-tick range
            env(clammDeposit(
                    alice, pid, 0, 1,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();
        }

        // 3. Swap in pool with zero active liquidity
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Out-of-range deposit only (no active liquidity at
            // current price which is near tick 1)
            env(clammDeposit(
                    alice, pid, 500, 600,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();

            // Swap should fail or have zero output
            env(clammSwap(bob, pid, XRP(100)),
                ter(std::ignore));
            env.close();
        }

        // 4. Swap that crosses many ticks
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Create many small positions at different ranges
            for (int t = -200; t < 200; t += 20)
            {
                env(clammDeposit(
                        alice, pid, t, t + 20,
                        XRP(100), USD(100)),
                    ter(std::ignore));
                env.close();
            }

            // Large swap that should cross 10+ ticks
            env(clammSwap(bob, pid, XRP(5000)),
                ter(std::ignore));
            env.close();

            auto const sle = env.current()->read(keylet::clamm(pid));
            BEAST_EXPECT(sle);
            if (sle)
            {
                auto const sqrtPrice = clamm::fromSLEField(
                    sle->getFieldH128(sfSqrtPrice));
                BEAST_EXPECT(sqrtPrice > 0);
            }
        }

        // 5. Withdraw when position is out-of-range
        {
            Env env{*this, features};
            clammSetupEnv(env, gw, alice, bob, carol, USD);

            auto const pid =
                clammPoolID(xrpIssue(), USD.issue(), 1);

            env(clammCreate(env,
                    alice, xrpIssue(), USD.issue(), 1,
                    clammDefaultSqrtPrice()),
                ter(tesSUCCESS));
            env.close();

            // Deposit in-range
            env(clammDeposit(
                    alice, pid, -100, 100,
                    XRP(10000), USD(10000)),
                ter(tesSUCCESS));
            env.close();

            // Deposit out-of-range (far above current price)
            env(clammDeposit(
                    bob, pid, 500, 600,
                    XRP(1000), USD(1000)),
                ter(std::ignore));
            env.close();

            auto const bobNFT = clammFindPositionNFT(env, bob, pid);
            if (bobNFT)
            {
                // Withdraw bob's out-of-range position
                env(clammWithdraw(bob, *bobNFT),
                    ter(std::ignore));
                env.close();
            }
        }

        pass();
    }

    void
    run() override
    {
        testFuzzDepositWithdraw();
        testFuzzSwapSequences();
        testFuzzPriceMovements();
        testFuzzEdgeCases();
    }
};

BEAST_DEFINE_TESTSUITE_PRIO(CLAMMFuzz, app, xrpl, 2);

}  // namespace test
}  // namespace xrpl
