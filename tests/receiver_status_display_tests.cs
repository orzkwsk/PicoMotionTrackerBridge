using System;
using PicoOTBridge;
internal static class ReceiverStatusDisplayTests
{
    static void Check(bool condition, string name)
    {
        if (!condition) throw new Exception(name);
        Console.WriteLine("PASS " + name);
    }
    static int Main()
    {
        try
        {
            Check(!ReceiverStatusDisplay.IsFreshHeartbeat(100, 0), "uninitialized status is unknown");
            Check(!ReceiverStatusDisplay.IsFreshHeartbeat(100, -1), "negative timestamp rejected");
            Check(!ReceiverStatusDisplay.IsFreshHeartbeat(100, 101), "future status rejected");
            Check(ReceiverStatusDisplay.IsFreshHeartbeat(100, 100), "current heartbeat accepted");
            Check(ReceiverStatusDisplay.IsFreshHeartbeat(3000000001L, 1), "exact heartbeat deadline accepted");
            Check(!ReceiverStatusDisplay.IsFreshHeartbeat(3000000002L, 1), "stale heartbeat rejected");
            Check(!ReceiverStatusDisplay.IsFreshHeartbeat(long.MaxValue, 1), "large stale age cannot overflow fresh");
            Check(ReceiverStatusDisplay.CachedTrackers(5) == "5 cached tracker(s); pose freshness unknown",
                  "cache size does not claim live tracking");
            Console.WriteLine("Receiver status display: 8 tests PASS");
            return 0;
        }
        catch (Exception e) { Console.Error.WriteLine(e); return 1; }
    }
}
