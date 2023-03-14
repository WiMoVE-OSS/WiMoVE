#include "EventLoop.h"

#include <iostream>
#include <thread>

#include "NetworkRenderer.h"
#include "TimeoutException.h"
#include "VlanMissingException.h"
#include "logging/loginit.h"
#include "metrics/MetricsManager.h"
#include "prometheus/counter.h"

EventLoop::EventLoop(NetworkRenderer& renderer, SynchronizedQueue<Station>& station_queue)
    : renderer(renderer),
      station_queue(station_queue),
      socket(std::chrono::seconds(1)),
      processing_time_histogram(MetricsManager::get_instance().get_event_histogram()) {}

void EventLoop::loop_nl_queue(const std::future<void>& future) {
    auto& station_counter_processed = MetricsManager::get_instance().get_station_counter_processed();
    while (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        std::unique_ptr<Station> station;
        try {
            station = station_queue.dequeue(std::chrono::seconds(1));
            station_counter_processed.Increment();
            WMLOG(DEBUG) << "setting up interface " << station->vlan_interface_name() << " for mac " << station->mac;
            try {
                renderer.setup_vni(station->vni());
                renderer.setup_station(*station);
            } catch (const VlanMissingException& e) {
                WMLOG(ERROR) << "error setting up interface " << station->vlan_interface_name() << ": " << e.what();
            } catch (const std::runtime_error& e) {
                WMLOG(ERROR) << "station could not be bridged to vxlan interface: " << e.what();
            }
            // TODO: Disconnect station on bridging failure
            processing_time_histogram.Observe(station->finished_processing());
        } catch (const TimeoutException& e) {
            continue;
        }
    }
}