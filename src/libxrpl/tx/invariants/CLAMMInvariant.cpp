#include <xrpl/tx/invariants/CLAMMInvariant.h>

#include <xrpl/basics/Log.h>
#include <xrpl/protocol/CLAMMCore.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/LedgerFormats.h>
#include <xrpl/protocol/TxFormats.h>

namespace xrpl {

void
ValidCLAMM::visitEntry(
    bool isDelete,
    std::shared_ptr<SLE const> const& before,
    std::shared_ptr<SLE const> const& after)
{
    auto const typeAfter = after ? after->getType() : ltANY;
    auto const typeBefore = before ? before->getType() : ltANY;

    if (typeAfter == ltCLAMM || typeBefore == ltCLAMM)
    {
        if (!before && after)
            clammCreated_ = true;
        else
            clammModified_ = true;

        // Capture key fields from the after-SLE for safety-net checks
        if (after && typeAfter == ltCLAMM)
        {
            clammFeeTier_ = after->getFieldU8(sfFeeTier);
            if (after->isFieldPresent(sfSqrtPrice))
            {
                auto const sp = after->getFieldH128(sfSqrtPrice);
                clammSqrtPriceZero_ = (sp == base_uint<128>{});
            }
        }
    }

    if (typeAfter == ltCLAMM_TICK || typeBefore == ltCLAMM_TICK)
    {
        clammTickChanged_ = true;
        if (!before && after)
            ++clammTicksCreated_;
        if (isDelete)
            ++clammTicksDeleted_;
    }

    if (typeAfter == ltCLAMM_POSITION || typeBefore == ltCLAMM_POSITION)
    {
        clammPositionChanged_ = true;
        if (!before && after)
            ++clammPositionsCreated_;
        if (isDelete)
            ++clammPositionsDeleted_;
    }
}

bool
ValidCLAMM::finalize(
    STTx const& tx,
    TER const tec,
    XRPAmount const fee,
    ReadView const& view,
    beast::Journal const& j)
{
    // If the CLAMM amendment is not enabled, no CLAMM objects should exist
    if (!view.rules().enabled(featureCLAMM))
    {
        if (clammCreated_ || clammModified_ || clammTickChanged_ ||
            clammPositionChanged_)
        {
            JLOG(j.fatal()) << "Invariant failed: CLAMM objects modified "
                               "without amendment enabled";
            return false;
        }
        return true;
    }

    // If the transaction failed, no CLAMM state should have been created
    if (!isTesSuccess(tec))
        return true;

    auto const txType = tx.getTxnType();

    switch (txType)
    {
        case ttCLAMM_CREATE:
            return finalizeCreate(tx, view, j);
        case ttCLAMM_DEPOSIT:
            return finalizeDeposit(tx, view, j);
        case ttCLAMM_WITHDRAW:
            return finalizeWithdraw(tx, view, j);
        case ttCLAMM_SWAP:
            return finalizeSwap(tx, view, j);
        case ttCLAMM_COLLECT_FEES:
            return finalizeCollectFees(tx, view, j);
        case ttCLAMM_VOTE:
            return finalizeVote(tx, view, j);
        case ttCLAMM_BID:
            return finalizeBid(tx, view, j);
        default:
            break;
    }

    return true;
}

bool
ValidCLAMM::finalizeCreate(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    if (!clammCreated_)
    {
        JLOG(j.fatal()) << "Invariant failed: CLAMMCreate did not create pool";
        return false;
    }

    // Safety-net: verify FeeTier is valid
    if (clammFeeTier_ && !isValidCLAMMFeeTier(*clammFeeTier_))
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMCreate has invalid FeeTier "
            << static_cast<unsigned>(*clammFeeTier_);
        return false;
    }
    return true;
}

bool
ValidCLAMM::finalizeDeposit(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // A deposit should create at least one position
    if (clammPositionsCreated_ == 0 && !tx.isFieldPresent(sfNFTokenID))
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMDeposit did not create position";
        return false;
    }
    return true;
}

bool
ValidCLAMM::finalizeWithdraw(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Withdrawal invariants checked on full implementation
    return true;
}

bool
ValidCLAMM::finalizeSwap(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Swap should modify the pool
    if (!clammModified_)
    {
        JLOG(j.fatal()) << "Invariant failed: CLAMMSwap did not modify pool";
        return false;
    }

    // Safety-net: SqrtPrice must remain positive after swap
    if (clammSqrtPriceZero_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMSwap resulted in zero SqrtPrice";
        return false;
    }
    return true;
}

bool
ValidCLAMM::finalizeCollectFees(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // CollectFees must update the position's fee snapshots
    if (!clammPositionChanged_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMCollectFees did not update position";
        return false;
    }
    return true;
}

bool
ValidCLAMM::finalizeVote(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Vote must modify the pool (VoteSlots/TradingFee)
    if (!clammModified_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMVote did not modify pool";
        return false;
    }
    return true;
}

bool
ValidCLAMM::finalizeBid(
    STTx const& tx,
    ReadView const& view,
    beast::Journal const& j) const
{
    // Bid must modify the pool (AuctionSlot)
    if (!clammModified_)
    {
        JLOG(j.fatal())
            << "Invariant failed: CLAMMBid did not modify pool";
        return false;
    }
    return true;
}

}  // namespace xrpl
