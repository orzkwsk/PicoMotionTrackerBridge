namespace PicoOTBridge
{
    // A live service heartbeat only proves the status writer is updating.
    // Shared status v1 does not carry per-pose receipt time or tracking validity.
    internal static class ReceiverStatusDisplay
    {
        internal static bool IsFreshHeartbeat(long nowNs, long updatedNs)
        {
            return updatedNs > 0 && nowNs >= updatedNs && nowNs - updatedNs <= 3000000000L;
        }

        internal static string CachedTrackers(uint count)
        {
            return count.ToString() + " cached tracker(s); pose freshness unknown";
        }
    }
}
