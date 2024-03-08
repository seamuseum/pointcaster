#pragma once

#include "../client_sync/sync_server.h"
#include "../midi/midi_device.h"
#include "../mqtt/mqtt_client.h"

#include "publishable_traits.h"
#include <initializer_list>
#include <type_traits>
#include <variant>

namespace pc::publisher {

// A variant that holds all of our publisher types
using Publisher = std::variant<mqtt::MqttClient *, midi::MidiDevice *,
			       client_sync::SyncServer *>;

// The list of all publishers in the application
extern std::vector<Publisher> _instances;
void add(Publisher publisher);
void remove(Publisher publisher);

// Publish a single value on a specific topic through all publishers
template <typename T>
void publish_all(const std::string_view topic, const T &data,
		 std::initializer_list<std::string_view> topic_nodes = {}) {
  for (auto &publisher : _instances) {
    std::visit(
        [&topic, &data, &topic_nodes](auto &publisher) {
	  publisher->publish(topic, data, topic_nodes);
        },
        publisher);
  }
}

} // namespace pc::publisher
