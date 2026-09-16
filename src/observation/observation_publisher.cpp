#include "pico_ot_backend/observation_publisher.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
namespace pico_ot {
bool ObservationPublisher::Start(uint16_t port, std::string* error) {
    if (!port) { if (error) *error = "Observation destination port must be nonzero"; return false; }
    destination_ = {"127.0.0.1",port};
    return socket_.Open(0,true,error,true,true);
}
void ObservationPublisher::Publish(PicoObservationAdapter& adapter, const bridge::Receiver& receiver, int64_t now) {
    for (auto& value : adapter.Collect(receiver,now)) {
        std::string bytes; monaka::protocol::v2::Error error;
        if (!adapter.StampSentAt(value,bridge::MonotonicNowNs()) ||
            !monaka::protocol::v2::EncodeEnvelope(value,bytes,error) ||
            !socket_.SendTo(destination_,reinterpret_cast<const uint8_t*>(bytes.data()),bytes.size())) ++errors_;
        else ++sent_;
    }
}
}
