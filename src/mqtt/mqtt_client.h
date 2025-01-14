#pragma once

#include "../logger.h"
#include "../publisher/publishable_traits.h"
#include "mqtt_client_config.gen.h"
#include <atomic>
#include <concurrentqueue/blockingconcurrentqueue.h>
#include <expected>
#include <future>
#include <memory>
#include <mqtt/client.h>
#include <msgpack.hpp>
#include <mutex>
#include <span>
#include <string_view>
#include <type_traits>

namespace pc::mqtt {

class MqttClient {
public:
  MqttClient(MqttClientConfiguration &config);
  ~MqttClient();

  MqttClient(const MqttClient &) = delete;
  MqttClient &operator=(const MqttClient &) = delete;
  MqttClient(MqttClient &&) = delete;
  MqttClient &operator=(MqttClient &&) = delete;

  void connect();
  void connect(const std::string_view uri);
  void connect(const std::string_view host_name, const int port);

  void disconnect();

  bool connecting() { return _connecting; };

  void set_uri(const std::string_view uri);
  void set_uri(const std::string_view hostname, int port);

  void publish(const std::string_view topic, const std::string_view message,
               std::initializer_list<std::string_view> topic_nodes = {});

  void publish(const std::string_view topic, const void *payload,
	       const std::size_t size,
	       std::initializer_list<std::string_view> topic_nodes = {},
	       bool empty = false);

  template <typename T>
  void publish(const std::string_view topic, const T &data,
	       std::initializer_list<std::string_view> topic_nodes = {}) {
    msgpack::sbuffer send_buffer;
    msgpack::pack(send_buffer, data);
    if constexpr (is_publishable_container_v<T>) {
      publish(topic, send_buffer.data(), send_buffer.size(),
	      std::move(topic_nodes), data.empty());
    } else if (pc::types::ScalarType<T>) {
      publish(topic, send_buffer.data(), send_buffer.size(),
	      std::move(topic_nodes));
    } else {
      pc::logger->error(
	  "Attempting to publish an unimplemented type through MQTT");
    }
  }

  void draw_imgui_window();

private:
  MqttClientConfiguration &_config;
  std::future<void> _connection_future;
  std::atomic_bool _connecting;
  std::unique_ptr<::mqtt::client> _client;

  struct MqttMessage {
    std::string topic;
    std::vector<std::byte> buffer;
  };

  std::jthread _sender_thread;
  moodycamel::BlockingConcurrentQueue<MqttMessage> _messages_to_publish;

  void send_messages(std::stop_token st);
};

} // namespace pc::mqtt
