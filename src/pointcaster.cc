#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <queue>
#include <random>
#include <set>
#include <span>
#include <thread>
#include <vector>

#include <serdepp/adaptor/toml11.hpp>
#include <serdepp/serde.hpp>
#include <toml.hpp>

#include <zpp_bits.h>

#ifdef _WIN32
#include <WinSock2.h>
#include <io.h>
#else
#include <unistd.h>
#endif

#include <zmq.hpp>

#include "logger.h"
#include "profiling.h"
#include "main_thread_dispatcher.h"
#include "path.h"
#include "pointer.h"
#include "session.gen.h"
#include "structs.h"

#include <Corrade/Utility/StlMath.h>
#include <Magnum/GL/Context.h>
#include <Magnum/GL/DefaultFramebuffer.h>
#include <Magnum/GL/PixelFormat.h>
#include <Magnum/GL/Renderer.h>
#include <Magnum/GL/Version.h>
#include <Magnum/Image.h>
#include <Magnum/ImageView.h>
#include <Magnum/Magnum.h>
#include <Magnum/Math/Color.h>
#include <Magnum/Math/FunctionsBatch.h>
#include <Magnum/PixelFormat.h>
#include <Magnum/Platform/Sdl2Application.h>
#include <Magnum/Primitives/Icosphere.h>
#include <Magnum/SceneGraph/Camera.h>
#include <Magnum/SceneGraph/Drawable.h>
#include <Magnum/SceneGraph/MatrixTransformation3D.h>
#include <Magnum/SceneGraph/Scene.h>

#include "fonts/IconsFontAwesome6.h"
#include <Magnum/ImGuiIntegration/Context.hpp>
#include <Magnum/ImGuiIntegration/Widgets.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <pointclouds.h>

#include "camera/camera_controller.h"
#include "client_sync/sync_server.h"
#include "devices/device.h"
#include "devices/usb.h"
#include "gui/widgets.h"
#include "modes.h"
#include "operators/session_operator_host.h"
#include "point_cloud_renderer.h"
#include "publisher/publisher.h"
#include "radio/radio.h"
#include "shaders/texture_display.h"
#include "snapshots.h"
#include "sphere_renderer.h"
#include "tween/tween_manager.h"
#include "uuid.h"
#include "wireframe_objects.h"

// TODO these need to be removed when initialisation loop is made generic
#include "devices/k4a/k4a_device.h"
#include <k4a/k4a.h>

#include "mqtt/mqtt_client.h"
#include "mqtt/mqtt_client_config.gen.h"

#include "midi/midi_device.h"

#include "osc/osc_client.h"
#include "osc/osc_server.h"

#include <tracy/Tracy.hpp>

namespace pc {

using namespace pc;
using namespace pc::types;
using namespace pc::devices;
using namespace pc::camera;
using namespace pc::radio;
using namespace pc::client_sync;
using namespace pc::snapshots;
using namespace pc::midi;
using namespace pc::osc;
using namespace pc::mqtt;
using namespace pc::tween;
using namespace pc::operators;
using namespace pc::parameters;
using namespace Magnum;
using namespace Math::Literals;

using pc::devices::Device;
using pc::devices::K4ADevice;

using uint = unsigned int;

using Object3D = Magnum::SceneGraph::Object<SceneGraph::MatrixTransformation3D>;
using Scene3D = Magnum::SceneGraph::Scene<SceneGraph::MatrixTransformation3D>;

struct SphereInstanceData {
    Matrix4 transformationMatrix;
    Matrix3x3 normalMatrix;
    Color3 color;
};

class CameraDisplayWindow : public Magnum::Platform::ApplicationWindow {
public:
  explicit CameraDisplayWindow(class PointCaster &application);

  pc::shaders::TextureDisplayShader _shader;
  GL::Mesh _mesh;
private:
  
  void drawEvent() override;
  void viewportEvent(ViewportEvent& event) override;

  PointCaster &application();
};

class PointCaster : public Platform::Application {
  friend CameraDisplayWindow;

public:
  explicit PointCaster(const Arguments &args);

  template <class Function, class... Args>
  void run_async(Function &&f, Args &&...args) {
    _async_tasks.emplace_back(f, args...);
  }

protected:
  PointCasterSession _session;
  std::mutex _session_devices_mutex;

  Mode _current_mode{Mode::Normal};
  std::array<char, modeline_buffer_size> _modeline_input =
      std::array<char, modeline_buffer_size>({});

  std::vector<std::jthread>
      _async_tasks;

  std::unique_ptr<Scene3D> _scene;
  std::unique_ptr<SceneGraph::DrawableGroup3D> _scene_root;

  std::optional<Vector2i> _display_resolution;

  std::vector<std::unique_ptr<CameraController>> _camera_controllers;
  std::optional<std::reference_wrapper<CameraController>> _interacting_camera_controller;

  std::unique_ptr<PointCloudRenderer> _point_cloud_renderer;
  std::unique_ptr<SphereRenderer> _sphere_renderer;

  std::unique_ptr<WireframeGrid> _ground_grid;

  std::unique_ptr<SessionOperatorHost> _session_operator_host;

  std::unique_ptr<Snapshots> _snapshots_context;

  std::unique_ptr<Radio> _radio;
  std::unique_ptr<MqttClient> _mqtt;
  std::unique_ptr<MidiDevice> _midi;
  std::unique_ptr<OscClient> _osc_client;
  std::unique_ptr<OscServer> _osc_server;
  std::unique_ptr<SyncServer> _sync_server;

#ifndef WIN32
  std::unique_ptr<UsbMonitor> _usb_monitor;
  std::mutex _usb_config_mutex;
#endif

  ImGuiIntegration::Context _imgui_context{NoCreate};

  ImFont *_font;
  ImFont *_mono_font;
  ImFont *_icon_font;

  /* Spheres rendering */
  GL::Mesh _sphere_mesh{NoCreate};
  GL::Buffer _sphere_instance_buffer{NoCreate};
  Shaders::PhongGL _sphere_shader{NoCreate};
  Containers::Array<SphereInstanceData> _sphere_instance_data;

  std::optional<CameraDisplayWindow> _secondary_window;

  void save_session();
  void save_session(std::filesystem::path file_path);
  void load_session(std::filesystem::path file_path);

  std::atomic_bool loading_device = false;
  void load_device(const DeviceConfiguration& config, std::string_view target_id = "");
  void open_kinect_sensors();

  void render_cameras();
  void publish_parameters();

  void draw_menu_bar();
  void draw_control_bar();
  void draw_main_viewport();
  void draw_viewport_controls(CameraController& selected_camera);
  void draw_camera_control_windows();
  void draw_devices_window();
  void draw_onscreen_log();
  void draw_modeline();

  Vector2i _restore_window_size;
  Vector2i _restore_window_position;
  bool _full_screen;
  void set_full_screen(bool full_screen);

  void quit();
  void save_and_quit();

  Timeline _timeline;
  std::vector<float> frame_durations;
  void draw_stats(const float delta_time);

  void drawEvent() override;
  void viewportEvent(ViewportEvent &event) override;

  void keyPressEvent(KeyEvent &event) override;
  void keyReleaseEvent(KeyEvent &event) override;

  void find_mode_keypress(KeyEvent &event);

  void textInputEvent(TextInputEvent &event) override;
  void mousePressEvent(MouseEvent &event) override;
  void mouseReleaseEvent(MouseEvent &event) override;
  void mouseMoveEvent(MouseMoveEvent &event) override;
  void mouseScrollEvent(MouseScrollEvent &event) override;
};

PointCaster::PointCaster(const Arguments &args)
    : Platform::Application(args, NoCreate) {

  pc::logger->info("This is pointcaster");

  MainThreadDispatcher::init();

  // Get OS resolution
  SDL_DisplayMode dm;
  if (SDL_GetDesktopDisplayMode(0, &dm) != 0)
    pc::logger->warn("Failed to get display resolution using SDL: {}", SDL_GetError());
  else {
    _display_resolution = {dm.w, dm.h};
    pc::logger->info("SDL2 returned display resolution: {}x{}", dm.w, dm.h);
  }

  // Set up the window

  Sdl2Application::Configuration conf;
  conf.setTitle("pointcaster");

  // TODO figure out how to persist window size accross launches
  if (_display_resolution.has_value()) {
    auto& resolution = _display_resolution.value();
    constexpr auto start_res_scale = 2.0f / 3.0f;
    constexpr auto start_ratio = 2.0f / 3.0f;
    auto start_width = int(resolution.x() / 1.5f * start_res_scale);
    auto start_height = int(start_width * start_ratio);
    conf.setSize({start_width, start_height}, {1.5f, 1.5f});
  }

  conf.setWindowFlags(Sdl2Application::Configuration::WindowFlag::Resizable);

  // Try 8x MSAA, fall back to zero if not possible.
  // Enable only 2x MSAA if we have enough DPI.
  GLConfiguration gl_conf;
  gl_conf.setSampleCount(8);
  if (!tryCreate(conf, gl_conf))
    create(conf, gl_conf.setSampleCount(0));

  // Set up ImGui
  ImGui::CreateContext();
  pc::gui::init_parameter_styles();

  // Don't save imgui layout to a file, handle it manually
  ImGui::GetIO().IniFilename = nullptr;

  const auto size = Vector2(windowSize()) / dpiScaling();

  // load fonts from resources
  Utility::Resource rs("data");

  auto font = rs.getRaw("AtkinsonHyperlegibleRegular");
  ImFontConfig font_config;
  font_config.FontDataOwnedByAtlas = false;
  constexpr auto font_size = 16.0f;
  _font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
      const_cast<char *>(font.data()), font.size(),
      font_size * framebufferSize().x() / size.x(), &font_config);

  auto mono_font = rs.getRaw("IosevkaArtisan");
  ImFontConfig mono_font_config;
  mono_font_config.FontDataOwnedByAtlas = false;
  const auto mono_font_size = 14.5f;
  _mono_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
      const_cast<char *>(mono_font.data()), mono_font.size(),
      mono_font_size * framebufferSize().x() / size.x(), &mono_font_config);

  auto font_icons = rs.getRaw("FontAwesomeSolid");
  static const ImWchar icons_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
  ImFontConfig icons_config;
  icons_config.MergeMode = true;
  icons_config.PixelSnapH = true;
  icons_config.FontDataOwnedByAtlas = false;
  const auto icon_font_size = 13.0f;
  icons_config.GlyphMinAdvanceX = icon_font_size;
  const auto icon_font_size_pixels =
      icon_font_size * framebufferSize().x() / size.x();

  _icon_font = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
      const_cast<char *>(font_icons.data()), font_icons.size(),
      icon_font_size_pixels, &icons_config, icons_ranges);

  // enable window docking
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  // ImGui::GetIO().ConfigFlags |= ImGuiDockNodeFlags_PassthruCentralNode;
  // ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  // enable keyboard tab & arrows navigation
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  // for editing parameters with the keyboard
  auto backspace = ImGui::GetIO().KeyMap[ImGuiKey_Backspace];
  ImGui::GetIO().AddInputCharacter(backspace);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace, true);
  ImGui::GetIO().AddKeyEvent(ImGuiKey_Backspace, false);

  _imgui_context = ImGuiIntegration::Context(
      *ImGui::GetCurrentContext(), Vector2(windowSize()) / dpiScaling(),
      windowSize(), framebufferSize());

  // Set up blending to be used by imgui
  Magnum::GL::Renderer::setBlendEquation(
      Magnum::GL::Renderer::BlendEquation::Add,
      Magnum::GL::Renderer::BlendEquation::Add);
  Magnum::GL::Renderer::setBlendFunction(
      Magnum::GL::Renderer::BlendFunction::SourceAlpha,
      Magnum::GL::Renderer::BlendFunction::OneMinusSourceAlpha);

  // Set up scene
  // TODO should drawable groups go inside each camera controller?
  _scene = std::make_unique<Scene3D>();
  _scene_root = std::make_unique<SceneGraph::DrawableGroup3D>();

  _ground_grid =
      std::make_unique<WireframeGrid>(_scene.get(), _scene_root.get());
  _ground_grid->transform(Matrix4::scaling(Vector3(1.0f)) *
                          Matrix4::translation(Vector3(0, 0, 0)));

  const auto fetch_session_devices = [this] {
    std::lock_guard lock(this->_session_devices_mutex);
    return this->_session.devices;
  };

#ifndef WIN32
  const auto fetch_usb_config = [this] {
    std::lock_guard lock(this->_usb_config_mutex);
    return this->_session.usb.value();
  };
  _usb_monitor = std::make_unique<UsbMonitor>(fetch_usb_config, fetch_session_devices);
#endif

  // load last session
  auto data_dir = path::get_or_create_data_directory();
  std::filesystem::path last_modified_session_file;
  std::filesystem::file_time_type last_write_time;

  for (const auto &entry : std::filesystem::directory_iterator(data_dir)) {
    if (!entry.is_regular_file() || entry.path().extension() != ".toml")
      continue;

    auto write_time = std::filesystem::last_write_time(entry);
    if (last_modified_session_file.empty() || write_time > last_write_time) {
      last_modified_session_file = entry.path();
      last_write_time = write_time;
    }
  }

  if (last_modified_session_file.empty()) {
    pc::logger->info("No previous session file found. Creating new session.");
    _session = {.id = pc::uuid::word()};
    auto file_path = data_dir / (_session.id + ".toml");
    save_session(file_path);
  } else {
    pc::logger->info("Found previous session file");
    load_session(last_modified_session_file);
  }

  // If there are no cameras in the scene, initialise at least one
  if (_camera_controllers.empty()) {
    auto _default_camera_controller =
        std::make_unique<CameraController>(this, _scene.get());
    // TODO viewport size needs to be dynamic
    _default_camera_controller->camera().setViewport(
        GL::defaultFramebuffer.viewport().size());
    _camera_controllers.push_back(std::move(_default_camera_controller));
  }

  // if there is no usb configuration, initialise a default
  if (!_session.usb.has_value()) {
    _session.usb = pc::devices::UsbConfiguration{};
  }
  declare_parameters("usb", _session.usb.value());

  const auto viewport_size = GL::defaultFramebuffer.viewport().size();

  GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
  // render particles as sprites
  GL::Renderer::enable(GL::Renderer::Feature::ProgramPointSize);

  // set background color
  GL::Renderer::setClearColor(0x000000_rgbf);

  _point_cloud_renderer = std::make_unique<PointCloudRenderer>();
  _sphere_renderer = std::make_unique<SphereRenderer>();

  // set up the spheres that render skeleton joints
  /* Setup points (render as spheres) */
  {
    const std::size_t total_joint_count = K4ABT_JOINT_COUNT * 5;
    const Vector3 start_pos{-999, -999, -999};
    const Vector3 joint_size{0.015};

    _sphere_instance_data =
	Containers::Array<SphereInstanceData>{NoInit, total_joint_count};

    for (std::size_t i = 0; i < total_joint_count; i++) {
      /* Fill in the instance data. Most of this stays the same, except
         for the translation */
      _sphere_instance_data[i].transformationMatrix =
          Matrix4::translation(start_pos) * Matrix4::scaling(joint_size);
      _sphere_instance_data[i].normalMatrix =
          _sphere_instance_data[i].transformationMatrix.normalMatrix();
      _sphere_instance_data[i].color =
	  Color3{Vector3(std::rand(), std::rand(), std::rand()) /
		 Magnum::Float(RAND_MAX)};
    }

    _sphere_shader =
        Shaders::PhongGL{Shaders::PhongGL::Configuration{}.setFlags(
            Shaders::PhongGL::Flag::VertexColor |
            Shaders::PhongGL::Flag::InstancedTransformation)};
    _sphere_instance_buffer = GL::Buffer{};
    _sphere_mesh = MeshTools::compile(Primitives::icosphereSolid(2));
    _sphere_mesh.addVertexBufferInstanced(
        _sphere_instance_buffer, 1, 0, Shaders::PhongGL::TransformationMatrix{},
        Shaders::PhongGL::NormalMatrix{}, Shaders::PhongGL::Color3{});
    _sphere_mesh.setInstanceCount(_sphere_instance_data.size());
  }

  // initialise point cloud operator hosts
  _session_operator_host = std::make_unique<SessionOperatorHost>(
      _session.session_operator_host, *_scene.get(), *_scene_root.get());

  // Start the timer, loop at 144 Hz max
  setSwapInterval(1);
  setMinimalLoopPeriod(7);

  if (!_session.radio.has_value()) {
    _session.radio = RadioConfiguration {};
  }
  _radio = std::make_unique<Radio>(*_session.radio, *_session_operator_host);

  if (!_session.mqtt.has_value()) {
    _session.mqtt = MqttClientConfiguration{};
  }
  _mqtt = std::make_unique<MqttClient>(*_session.mqtt);

  if (!_session.midi.has_value()) {
    _session.midi = MidiDeviceConfiguration{};
  }
  _midi = std::make_unique<MidiDevice>(*_session.midi);

  if (!_session.osc_client.has_value()) {
    _session.osc_client = OscClientConfiguration{};
  }
  _osc_client = std::make_unique<OscClient>(*_session.osc_client);

  if (!_session.osc_server.has_value()) {
    _session.osc_server = OscServerConfiguration{};
  }
  _osc_server = std::make_unique<OscServer>(*_session.osc_server);

  if (!_session.sync_server.has_value()) {
    _session.sync_server = SyncServerConfiguration{};
  }
  _sync_server = std::make_unique<SyncServer>(*_session.sync_server);

  _snapshots_context = std::make_unique<Snapshots>();

  TweenManager::create();
  _timeline.start();

  // _secondary_window.emplace(*this);
}

CameraDisplayWindow::CameraDisplayWindow(PointCaster &application)
  : Platform::ApplicationWindow{application, Configuration{}.setTitle("Hey").setSize({400, 400})} {
  pc::logger->debug("Secondary window initialisation");

  struct Vertex {
    Vector2 position;
    Vector2 textureCoordinates;
  };
  const Vertex vertices[] = {{{-1.0f, -1.0f}, {0.0f, 0.0f}},
                             {{1.0f, -1.0f}, {1.0f, 0.0f}},
                             {{-1.0f, 1.0f}, {0.0f, 1.0f}},
                             {{1.0f, 1.0f}, {1.0f, 1.0f}}};

  GL::Buffer buffer;
  buffer.setData(Containers::arrayView(vertices), GL::BufferUsage::StaticDraw);

  _mesh.setPrimitive(MeshPrimitive::TriangleStrip)
      .addVertexBuffer(std::move(buffer), 0,
                       pc::shaders::TextureDisplayShader::Position{},
                       pc::shaders::TextureDisplayShader::TextureCoordinates{})
      .setCount(4);


  GL::defaultFramebuffer.clear(GL::FramebufferClear::Color |
			       GL::FramebufferClear::Depth);
}

inline PointCaster &CameraDisplayWindow::application() {
  return static_cast<PointCaster &>(Platform::ApplicationWindow::application());
}

void PointCaster::quit() {
  Device::attached_devices.clear();
  exit(0);
}

void PointCaster::save_and_quit() {
  save_session();
  quit();
}

void PointCaster::save_session() {
  auto data_dir = path::get_or_create_data_directory();
  auto file_path = data_dir / (_session.id + ".toml");
  save_session(file_path);
}

void PointCaster::save_session(std::filesystem::path file_path) {
  pc::logger->info("Saving session to {}", file_path.string());

  // save imgui layout to an adjacent file
  std::size_t imgui_layout_size;
  auto imgui_layout_data = ImGui::SaveIniSettingsToMemory(&imgui_layout_size);
  std::vector<char> layout_data(imgui_layout_data,
				imgui_layout_data + imgui_layout_size);
  std::filesystem::path layout_file_path = file_path;
  layout_file_path.replace_extension(".layout");
  std::ofstream layout_file(layout_file_path, std::ios::binary);
  layout_file.write(layout_data.data(), layout_data.size());

  auto output_session = _session;

  output_session.devices.clear();
  for (auto &device : Device::attached_devices) {
    output_session.devices.emplace(device->id(), device->config());
  }

  const auto uninitialized_or_default = [](auto &optional_obj) {
    if (!optional_obj.has_value()) {
      return true;
    }
    using T = typename std::decay<decltype(optional_obj.value())>::type;
    const auto type_info = serde::type_info<T>;
    const auto index_sequence = std::make_index_sequence<T::MemberCount>{};

    const auto is_default_member = [&](auto index) {
      auto &member_ref = type_info.template member<decltype(index)::value>(
          optional_obj.value());
      return member_ref == std::get<index>(T::Defaults);
    };

    return [&]<std::size_t... Is>(std::index_sequence<Is...>) {
      return (... &&
              is_default_member(std::integral_constant<std::size_t, Is>{}));
    }(index_sequence);
  };

  // remove the value from any optional configuration structures that are
  // uninitialised or default. to not store unneeded values in our save file

  if (uninitialized_or_default(output_session.radio)) {
    output_session.radio.reset();
  }
  if (uninitialized_or_default(output_session.usb)) {
    output_session.usb.reset();
  }
  if (uninitialized_or_default(output_session.mqtt)) {
    output_session.mqtt.reset();
  }
  if (uninitialized_or_default(output_session.midi)) {
    output_session.midi.reset();
  }

  output_session.cameras.clear();
  for (auto &camera : _camera_controllers) {
    auto config = camera->config();
    config.name = {};
    config.id = {};
    config.show_window = true;
    if (!config.empty()) {
      output_session.cameras.emplace(camera->name(), camera->config());
    }
  }

  output_session.published_params = published_parameter_topics();

  auto session_toml = serde::serialize<serde::toml_v>(output_session);
  std::ofstream(file_path, std::ios::binary) << toml::format(session_toml);
}

void PointCaster::load_session(std::filesystem::path file_path) {
  pc::logger->info("Loading state from {}", file_path.string());

  std::ifstream file(file_path, std::ios::binary);

  if (!file) {
    pc::logger->warn("Failed to open file");
    return;
  }

  try {
    toml::value file_toml = toml::parse(file);
    _session = serde::deserialize<PointCasterSession>(file_toml);
  } catch (const toml::syntax_error &e) {
    pc::logger->warn("Failed to parse config file toml");
    pc::logger->warn(e.what());
    return;
  }

  // check if there is an adjacent .layout file
  // and load if so
  std::filesystem::path layout_file_path = file_path;
  layout_file_path.replace_extension(".layout");

  // and load if so
  std::ifstream layout_file(layout_file_path, std::ios::binary | std::ios::ate);
  if (layout_file.is_open()) {
    std::streamsize layout_size = layout_file.tellg();
    layout_file.seekg(0, std::ios::beg);

    std::vector<char> layout_data(layout_size);
    layout_file.read(layout_data.data(), layout_size);

    ImGui::LoadIniSettingsFromMemory(layout_data.data(), layout_size);

  } else {
    pc::logger->warn("Failed to open adjacent .layout file");
  }

  // get saved camera configurations and populate the cams list
  for (auto &[_, saved_camera_config] : _session.cameras) {
    auto saved_camera =
      std::make_unique<CameraController>(this, _scene.get(), saved_camera_config);
    saved_camera->camera().setViewport(
        GL::defaultFramebuffer.viewport().size());
    _camera_controllers.push_back(std::move(saved_camera));
  }

  if (!_session.usb.has_value()) {
    _session.usb = UsbConfiguration {};
  }

  if ((*_session.usb).open_on_launch) {
    if (_session.devices.empty()) {
      open_kinect_sensors();
    } else {
      // get saved device configurations and populate the device list
      run_async([this] {
	for (auto &[device_id, device_config] : _session.devices) {
	  pc::logger->info("Loading device '{}' from config file", device_id);
	  load_device(device_config, device_id);
	}
      });
    }
  }

  if (_session.published_params.has_value()) {
    for (auto &parameter_id : *_session.published_params) {
      parameter_states.emplace(parameter_id, ParameterState::Publish);
    }
  }

  pc::logger->info("Loaded session '{}'", file_path.filename().string());
}

void PointCaster::render_cameras() {

  PointCloud points;

  auto skeletons = devices::scene_skeletons();

  for (auto &camera_controller : _camera_controllers) {

    auto& rendering_config = camera_controller->config().rendering;

    const auto frame_size =
      Vector2i{int(rendering_config.resolution[0] / dpiScaling().x()),
	       int(rendering_config.resolution[1] / dpiScaling().y())};

    if (frame_size.x() < 1 || frame_size.y() < 1) continue;

    camera_controller->setup_frame(frame_size);

    // TODO: pass selected physical cameras into the
    // synthesise_point_cloud function 
    // - make sure to cache already synthesised configurations
    if (points.empty()) {

      points = devices::synthesized_point_cloud({*_session_operator_host});

      if (rendering_config.snapshots)
        points += snapshots::point_cloud();
      _point_cloud_renderer->points = points;
      _point_cloud_renderer->setDirty();
    }

    // enable or disable wireframe ground depending on camera settings
    _ground_grid->set_visible(rendering_config.ground_grid);

    // draw shaders

    _point_cloud_renderer->draw(camera_controller->camera(),
				rendering_config);

    if (rendering_config.skeletons) {
      if (!skeletons.empty()) {
	int i = 0;
	for (auto &skeleton : skeletons) {
	  for (auto &joint : skeleton) {
	    auto pos = joint.first;
	    _sphere_instance_data[i].transformationMatrix.translation() = {
		pos.x / 1000.0f, pos.y / 1000.0f, pos.z / 1000.0f};
	    i++;
          }
        }
	_sphere_instance_buffer.setData(_sphere_instance_data,
					GL::BufferUsage::DynamicDraw);
	GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
	_sphere_shader
	    .setProjectionMatrix(camera_controller->camera().projectionMatrix())
	    .setTransformationMatrix(camera_controller->camera().cameraMatrix())
	    .setNormalMatrix(
		camera_controller->camera().cameraMatrix().normalMatrix())
	    .draw(_sphere_mesh);
	GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
      }
    }

    // render camera
    camera_controller->camera().draw(*_scene_root);

    camera_controller->dispatch_analysis();
  }

  GL::defaultFramebuffer.bind();
}

void PointCaster::load_device(const DeviceConfiguration& config, std::string_view target_id) {
  loading_device = true;
  try {
    auto device = std::make_shared<K4ADevice>(config, target_id);
    Device::attached_devices.push_back(device);
  } catch (k4a::error e) {
    pc::logger->error(e.what());
  } catch (...) {
    pc::logger->error("Failed to open device. (Unknown exception)");
  }
  loading_device = false;
}

void PointCaster::open_kinect_sensors() {
  run_async([this] {
    loading_device = true;
    const auto open_device_count = Device::attached_devices.size();
    const auto attached_device_count = k4a::device::get_installed_count();
    pc::logger->info("Found {} attached k4a devices", (int)attached_device_count);
    for (std::size_t i = open_device_count; i < attached_device_count; i++) {
      load_device({});
    }
    loading_device = false;
  });
}

void PointCaster::draw_menu_bar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Quit", "q"))
        save_and_quit();
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {

      constexpr auto window_item = [](const char *item_name,
                                      const char *shortcut_key,
                                      bool &window_toggle) {
        ImGui::BeginDisabled();
        ImGui::Checkbox((std::string("##Toggle_Window_") + item_name).data(),
                        &window_toggle);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::MenuItem(item_name, shortcut_key))
          window_toggle = !window_toggle;
      };

      window_item("Global Transform", "g", _session.layout.show_global_transform_window);
      window_item("Devices", "d", _session.layout.show_devices_window);
      window_item("RenderStats", "f", _session.layout.show_stats);
      // if (_session.mqtt.has_value()) {
      // 	window_item("MQTT", "m", (*_session.mqtt).show_window : false);
      // }

      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void PointCaster::draw_control_bar() {

  constexpr auto control_bar_flags = ImGuiWindowFlags_NoScrollbar |
                                     ImGuiWindowFlags_NoSavedSettings |
                                     ImGuiWindowFlags_MenuBar;

  if (ImGui::BeginViewportSideBar("##ControlBar", ImGui::GetMainViewport(),
                                  ImGuiDir_Up, ImGui::GetFrameHeight(),
                                  control_bar_flags)) {
    if (ImGui::BeginMenuBar()) {
      if (ImGui::Button(ICON_FA_FLOPPY_DISK)) {
        save_session();
      }

      if (ImGui::Button(ICON_FA_FOLDER_OPEN)) {
      }
      ImGui::EndMenuBar();
    }

    ImGui::End();
  }

  // ImGui::SetNextWindowPos({0, 20});

  // // Extend width to viewport width
  // ImGui::SetNextWindowSize({100, 100});

  // constexpr ImGuiWindowFlags control_bar_flags =
  //     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
  //     ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoScrollbar |
  //     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDecoration;

  // if (ImGui::Begin("ControlBar", nullptr, control_bar_flags)) {
  //   ImGui::Text("eyo");
  //   ImGui::SameLine();
  //   ImGui::Button("oh");
  //   ImGui::SameLine();
  //   ImGui::Button("woh");
  //   ImGui::End();
  // }
}

void PointCaster::draw_onscreen_log() {
  // if draw log
  ImVec2 log_window_size{400, 150};
  const auto viewport_size = ImGui::GetWindowSize();

  using namespace std::chrono_literals;
  auto latest_messages = pc::logger_lines(10, 10s);

  if (latest_messages.size() > 0) {
    ImGui::SetNextWindowPos({viewport_size.x - log_window_size.x,
			     viewport_size.y - log_window_size.y});
    ImGui::SetNextWindowSize({log_window_size.x, log_window_size.y});
    ImGui::SetNextWindowBgAlpha(0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushFont(_mono_font);
    constexpr auto log_window_flags =
	ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoFocusOnAppearing |
	ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDocking;
    ImGui::Begin("log", nullptr, log_window_flags);
    ImGui::PushTextWrapPos(log_window_size.x);
    for (auto log_entry : latest_messages) {
      ImGui::Spacing();

      auto [log_level, message] = log_entry;

      switch (log_level) {
      case spdlog::level::info:
	ImGui::PushStyleColor(ImGuiCol_Text,
			      ImVec4(1.0f, 1.0f, 1.0f, 1.0f)); // Green
	ImGui::Text(" [info]");
	ImGui::SameLine();
	break;
      case spdlog::level::warn:
	ImGui::PushStyleColor(ImGuiCol_Text,
			      ImVec4(1.0f, 1.0f, 0.0f, 1.0f)); // Yellow
	ImGui::Text(" [warn]");
	ImGui::SameLine();
	break;
      case spdlog::level::err:
	ImGui::PushStyleColor(ImGuiCol_Text,
			      ImVec4(1.0f, 0.0f, 0.0f, 1.0f)); // Red
	ImGui::Text("[error]");
	ImGui::SameLine();
	break;
      case spdlog::level::debug:
	ImGui::PushStyleColor(ImGuiCol_Text,
			      ImVec4(0.0f, 1.0f, 0.0f, 1.0f)); // Red
	ImGui::Text("[debug]");
	ImGui::SameLine();
	break;
      case spdlog::level::critical:
	ImGui::PushStyleColor(ImGuiCol_Text,
			      ImVec4(1.0f, 0.0f, 1.0f, 1.0f)); // Red
	ImGui::Text(" [crit]");
	ImGui::SameLine();
	break;
      }
      ImGui::TextUnformatted(message.c_str());
      ImGui::PopStyleColor();
      ImGui::SetScrollHereY();
    }
    ImGui::End();
    ImGui::PopFont();
    ImGui::PopStyleVar();
  }
}

void PointCaster::draw_modeline() {
  using namespace catpuccin::imgui;

  constexpr auto modeline_height = 20;
  constexpr auto modeline_color = mocha_crust;

  ImGui::PushID("modeline");

  ImGui::PushStyleColor(ImGuiCol_WindowBg, modeline_color);
  ImGui::PushStyleColor(ImGuiCol_Border, modeline_color);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});

  const auto viewport_size = ImGui::GetMainViewport()->Size;
  const ImVec2 modeline_size{viewport_size.x, modeline_height};
  const ImVec2 modeline_min {0, viewport_size.y - modeline_size.y};
  const ImVec2 modeline_max {viewport_size.x, viewport_size.y};

  ImGui::SetNextWindowPos(modeline_min);
  ImGui::SetNextWindowSize(modeline_size);

  auto window_flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings;
  if (_current_mode != Mode::Find) window_flags |= ImGuiWindowFlags_NoInputs;

  ImGui::Begin("modeline", nullptr, window_flags);

  if (_current_mode == Mode::Find) {
    ImGui::Text("/");
    ImGui::SameLine();
    ImGui::SetKeyboardFocusHere();

    if (ImGui::InputText("##modeline.find", _modeline_input.data(),
			 modeline_buffer_size)) {
    };
  }

  ImGui::End();

  ImGui::PopStyleVar();
  ImGui::PopStyleVar();

  ImGui::PopStyleColor();
  ImGui::PopStyleColor();

  ImGui::PopID();;
}

void PointCaster::find_mode_keypress(KeyEvent &event) {
  const auto &key = event.key();
  if (key == KeyEvent::Key::Enter) {
    _current_mode = Mode::NavigateMatch;
    _modeline_input.fill({});
  } else if (key == KeyEvent::Key::Esc) {
    _current_mode = Mode::Normal;
    _modeline_input.fill({});
  } else if (_imgui_context.handleKeyPressEvent(event)) {
    event.setAccepted(true);
  }
}

void PointCaster::draw_main_viewport() {

  ImGuiWindowClass docking_viewport_class = {};

  ImGuiID id =
      ImGui::DockSpaceOverViewport(nullptr,
                                   ImGuiDockNodeFlags_NoDockingInCentralNode |
                                       ImGuiDockNodeFlags_PassthruCentralNode,
                                   nullptr);
  ImGuiDockNode *node = ImGui::DockBuilderGetCentralNode(id);

  ImGuiWindowClass central_always = {};
  central_always.DockNodeFlagsOverrideSet |=
      ImGuiDockNodeFlags_NoTabBar | ImGuiDockNodeFlags_NoDockingOverMe;
  ImGui::SetNextWindowClass(&central_always);
  ImGui::SetNextWindowDockID(node->ID, ImGuiCond_Always);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});

  if (ImGui::Begin("CamerasRoot")) {
    ImGui::PopStyleVar();

    constexpr auto camera_tab_bar_flags =
        ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_NoTooltip;

    if (ImGui::BeginTabBar("Cameras"), camera_tab_bar_flags) {

      // button for creating a new camera
      auto new_camera_index = -1;
      if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
        auto new_camera = std::make_unique<CameraController>(this, _scene.get());
        _camera_controllers.push_back(std::move(new_camera));
        new_camera_index = _camera_controllers.size() - 1;
        pc::logger->info("new camera: {}", new_camera_index);
      }

      _interacting_camera_controller = std::nullopt;

      for (int i = 0; i < _camera_controllers.size(); i++) {
        auto &camera_controller = _camera_controllers.at(i);
	const auto camera_config = camera_controller->config();

        ImGuiTabItemFlags tab_item_flags = ImGuiTabItemFlags_None;
        if (new_camera_index == i)
          tab_item_flags |= ImGuiTabItemFlags_SetSelected;

	if (ImGui::BeginTabItem(camera_controller->name().data(), nullptr,
				tab_item_flags)) {

          const auto window_size = ImGui::GetWindowSize();

	  auto *tab_bar = ImGui::GetCurrentTabBar();
	  float tab_bar_height =
	      (tab_bar->BarRect.Max.y - tab_bar->BarRect.Min.y) + 5;
	  // TODO 5 pixels above? where's it come from

          if (_session.layout.hide_ui) {
	    tab_bar_height = 0;
            ImGui::SetCursorPosY(0);
          }

	  const auto draw_frame_labels =
	      [&camera_controller](ImVec2 viewport_offset) {
		ImGui::PushStyleColor(ImGuiCol_Text, {1.0f, 1.0f, 0.0f, 1.0f});
		for (auto label : camera_controller->labels()) {
		  auto &pos = label.position;
		  auto x = static_cast<float>(viewport_offset.x + pos.x);
		  auto y = static_cast<float>(viewport_offset.y + pos.y);
		  ImGui::SetCursorPos({x, y});
		  ImGui::Text("%s", label.text.data());
		}
		ImGui::PopStyleColor();
	      };

          auto rendering = camera_config.rendering;
          auto scale_mode = rendering.scale_mode;
	  const Vector2 frame_space{window_size.x,
				    window_size.y - tab_bar_height};
	  camera_controller->viewport_size = frame_space;

          if (scale_mode == (int)ScaleMode::Span) {

            auto image_pos = ImGui::GetCursorPos();
            ImGuiIntegration::image(camera_controller->color_frame(),
                                    {frame_space.x(), frame_space.y()});

            auto analysis = camera_config.analysis;

            if (analysis.enabled && (analysis.contours.draw)) {
              ImGui::SetCursorPos(image_pos);
              ImGuiIntegration::image(camera_controller->analysis_frame(),
                                      {frame_space.x(), frame_space.y()});
	      draw_frame_labels(image_pos);
            }
	  } else if (scale_mode == (int)ScaleMode::Letterbox) {

            float width, height, horizontal_offset, vertical_offset;

            auto frame_aspect_ratio = rendering.resolution[0] /
                                static_cast<float>(rendering.resolution[1]);
	    auto space_aspect_ratio = frame_space.x() / frame_space.y();

	    constexpr auto frame_spacing = 5.0f;

	    if (frame_aspect_ratio > space_aspect_ratio) {
	      width = frame_space.x();
	      height = std::round(width / frame_aspect_ratio);
	      horizontal_offset = 0.0f;
	      vertical_offset =
		  std::max(0.0f, (frame_space.y() - height) / 2.0f - frame_spacing);
	    } else {
	      height = frame_space.y() - 2 * frame_spacing;
	      width = std::round(height * frame_aspect_ratio);
	      vertical_offset = 0.0f;
	      horizontal_offset =
		  std::max(0.0f, (frame_space.x() - width) / 2.0f);
	    }

            ImGui::Dummy({horizontal_offset, vertical_offset});
	    if (horizontal_offset != 0.0f) ImGui::SameLine();

            constexpr auto border_size = 1.0f;
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, border_size);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 0});

	    ImGui::BeginChildFrame(ImGui::GetID("letterboxed"),
				   {width, height});

            auto image_pos = ImGui::GetCursorPos();
            ImGuiIntegration::image(camera_controller->color_frame(),
                                    {width, height});

            auto &analysis = camera_controller->config().analysis;

            if (analysis.enabled && (analysis.contours.draw)) {
              ImGui::SetCursorPos(image_pos);
              ImGuiIntegration::image(camera_controller->analysis_frame(),
                                      {width, height});
	      draw_frame_labels(image_pos);
            }

            ImGui::EndChildFrame();

	    ImGui::PopStyleVar();
            ImGui::PopStyleVar();

	    if (horizontal_offset != 0.0f) ImGui::SameLine();
            ImGui::Dummy({horizontal_offset, vertical_offset});
	  }

          if (!_session.layout.hide_ui) {
            // draw_viewport_controls(*camera_controller);
	    if (_session.layout.show_log) {
	      draw_onscreen_log();
	    }
          }

          if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
	    _interacting_camera_controller = *camera_controller;
          }

          ImGui::EndTabItem();
        }
      }
      ImGui::EndTabBar();
    }

    ImGui::End(); // "CamerasRoot"
  }
}

void PointCaster::draw_viewport_controls(CameraController &selected_camera) {
  auto& camera_config = selected_camera.config();
  const auto viewport_window_size = ImGui::GetWindowSize();
  const auto viewport_window_pos = ImGui::GetWindowPos();

  constexpr auto button_size = ImVec2{28, 28};
  constexpr auto control_inset = ImVec2{5, 35};

  constexpr auto viewport_controls_flags =
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
  ImGui::SetNextWindowSize({0, 0}); // auto size
  ImGui::Begin("ViewportControls", nullptr, viewport_controls_flags);

  constexpr auto draw_button = [button_size](auto content,
					     std::function<void()> action,
					     bool toggled = false) {
    ImGui::Spacing();
    ImGui::SameLine();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {0, 9});

    if (toggled) {
      const auto toggled_color =
	  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
      ImGui::PushStyleColor(ImGuiCol_Button, toggled_color);
    }

    if (ImGui::Button(content, button_size))
      action();

    if (toggled)
      ImGui::PopStyleColor();

    ImGui::PopStyleVar();
  };

  ImGui::PushFont(_icon_font);

  draw_button(ICON_FA_SLIDERS, [&] {
    camera_config.show_window = !camera_config.show_window;
   }, camera_config.show_window);

  ImGui::Dummy({0, 7});

  draw_button(ICON_FA_ENVELOPE, [&] {
    if (camera_config.rendering.scale_mode == (int)ScaleMode::Span)
      camera_config.rendering.scale_mode = (int)ScaleMode::Letterbox;
    else camera_config.rendering.scale_mode = (int)ScaleMode::Span;
  }, camera_config.rendering.scale_mode == (int)ScaleMode::Letterbox);

  ImGui::PopFont();

  ImGui::SetWindowPos({viewport_window_pos.x + control_inset.x,
		       viewport_window_pos.y + control_inset.y});

  ImGui::End();
  ImGui::PopStyleVar();
}

void PointCaster::draw_camera_control_windows() {
  for (const auto &camera_controller : _camera_controllers) {
    if (!camera_controller->config().show_window) continue;
    ImGui::SetNextWindowSize({250.0f, 400.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin(camera_controller->name().data());
    camera_controller->draw_imgui_controls();
    ImGui::End();
  }
}

void PointCaster::draw_devices_window() {
  ImGui::SetNextWindowPos({50.0f, 50.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({250.0f, 400.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);
  ImGui::Begin("Devices", nullptr);

  {

#ifndef WIN32
    {
      std::lock_guard lock(_usb_config_mutex);
      pc::gui::draw_parameters("usb", struct_parameters.at("usb"));
    }
#endif
    ImGui::Dummy({0, 4});
    ImGui::Dummy({10, 0});
    ImGui::SameLine();
    if (ImGui::Button("Open")) {
      open_kinect_sensors();
    }
    ImGui::SameLine();
    ImGui::Dummy({4, 0});
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
      Device::attached_devices.clear();
    }
    ImGui::Dummy({0, 12});
  }

  ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.8f);
  
  for (auto &device : Device::attached_devices) {
    auto& config = device->config();
    ImGui::SetNextItemOpen(config.unfolded);
    if (ImGui::CollapsingHeader(device->name.c_str())) {
      config.unfolded = true;
      ImGui::Dummy({0, 8});
      ImGui::Dummy({8, 0});
      ImGui::SameLine();
      ImGui::TextDisabled("%s", device->id().c_str());
      ImGui::Dummy({0, 6});
      device->draw_imgui_controls();
    } else {
      config.unfolded = false;
    }
  }
  ImGui::PopItemWidth();

  if (loading_device) {
    ImGui::Dummy({8, 0});
    ImGui::SameLine();
    ImGui::TextDisabled("Loading device...");
  }

  ImGui::End();
}

void PointCaster::draw_stats(const float delta_time) {
  ImGui::PushID("FrameStats");
  ImGui::SetNextWindowPos({50.0f, 200.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({200.0f, 100.0f}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);
  ImGui::Begin("Frame Stats", nullptr);
  ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.8f);

  // calculate the mean, min and max frame times from our last 60 frames
  frame_durations.push_back(delta_time);
  constexpr auto frames_to_average = 60 * 2; // 2 seconds
  if (frame_durations.size() < frames_to_average) {
    ImGui::Text("Gathering data...");
  } else {
    frame_durations.erase(frame_durations.begin()); // pop_front
    const float avg_duration =
        std::reduce(frame_durations.begin(), frame_durations.end()) /
        frame_durations.size();
    const auto minmax_duration =
        std::minmax_element(frame_durations.begin(), frame_durations.end());

    if (ImGui::CollapsingHeader("Rendering", true)) {
      ImGui::Text("Frame Duration");
      ImGui::BeginTable("duration", 2);
      ImGui::TableNextColumn();
      ImGui::Text("Average");
      ImGui::TableNextColumn();
      ImGui::Text("%.2fms", avg_duration * 1000);
      ImGui::TableNextColumn();
      ImGui::Text("Min");
      ImGui::TableNextColumn();
      ImGui::Text("%.2fms", *minmax_duration.first * 1000);
      ImGui::TableNextColumn();
      ImGui::Text("Max");
      ImGui::TableNextColumn();
      ImGui::Text("%.2fms", *minmax_duration.second * 1000);
      ImGui::EndTable();
      ImGui::Spacing();
      ImGui::Text("%.0f FPS", 1000.0f / (avg_duration * 1000));
    }

    for (auto &camera_controller : _camera_controllers) {
      if (ImGui::CollapsingHeader(camera_controller->name().data())) {
	if (camera_controller->config().analysis.enabled) {
	  ImGui::Text("Analysis Duration");
	  ImGui::BeginTable("analysis_duration", 2);
	  ImGui::TableNextColumn();
	  ImGui::Text("Current");
	  ImGui::TableNextColumn();
	  auto duration = camera_controller->analysis_time();
	  ImGui::Text("%ims", duration);
          ImGui::EndTable();
	  ImGui::Text("%.0f FPS", 1000.0f / duration);
	}
      }
    }
  }

  ImGui::PopItemWidth();
  ImGui::End();
  ImGui::PopID();
}

auto output_count = 0;

void PointCaster::drawEvent() {

  const auto delta_time = _timeline.previousFrameDuration();
  const auto delta_ms = static_cast<int>(delta_time * 1000);
  TweenManager::instance()->tick(delta_ms);

  std::function<void()> main_thread_callback;
  while (MainThreadDispatcher::try_dequeue(main_thread_callback)) {
    main_thread_callback();
  }

  GL::defaultFramebuffer.clear(GL::FramebufferClear::Color |
			       GL::FramebufferClear::Depth);

  render_cameras();


  _imgui_context.newFrame();
  pc::gui::begin_gui_helpers(_current_mode, _modeline_input);

  // Enable text input, if needed/
  if (ImGui::GetIO().WantTextInput && !isTextInputActive())
    startTextInput();
  else if (!ImGui::GetIO().WantTextInput && isTextInputActive())
    stopTextInput();

  // Draw gui windows

  // draw_menu_bar();
  // draw_control_bar();

  draw_main_viewport();

  if (!_session.layout.hide_ui) {
    draw_camera_control_windows();
    if (_session.layout.show_devices_window)
      draw_devices_window();
    if (_session.layout.show_stats)
      draw_stats(delta_time);
    if (_session.layout.show_radio_window)
      _radio->draw_imgui_window();
    if (_session.layout.show_snapshots_window)
      _snapshots_context->draw_imgui_window();
    if (_session.layout.show_global_transform_window)
      _session_operator_host->draw_imgui_window();

    if (_session.mqtt.has_value() && (*_session.mqtt).show_window)
      _mqtt->draw_imgui_window();

    if (_session.midi.has_value() && (*_session.midi).show_window) {
      ImGui::SetNextWindowSize({600, 400}, ImGuiCond_FirstUseEver);
      ImGui::Begin("MIDI");
      _midi->draw_imgui_window();
      ImGui::End();
    }

    if (_session.osc_client.has_value() && (*_session.osc_client).show_window)
      _osc_client->draw_imgui_window();

    if (_session.osc_server.has_value() && (*_session.osc_server).show_window)
      _osc_server->draw_imgui_window();

    draw_modeline();
  }

  _imgui_context.updateApplicationCursor(*this);

  // Render ImGui window
  GL::Renderer::enable(GL::Renderer::Feature::Blending);
  GL::Renderer::disable(GL::Renderer::Feature::FaceCulling);
  GL::Renderer::disable(GL::Renderer::Feature::DepthTest);
  GL::Renderer::enable(GL::Renderer::Feature::ScissorTest);

  _imgui_context.drawFrame();


  if (ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }

  GL::Renderer::disable(GL::Renderer::Feature::ScissorTest);
  GL::Renderer::enable(GL::Renderer::Feature::DepthTest);
  GL::Renderer::enable(GL::Renderer::Feature::FaceCulling);
  GL::Renderer::disable(GL::Renderer::Feature::Blending);

  // TODO this can be removed and if we want GL errors we can set
  // MAGNUM_GPU_VALIDATION=ON instead or run the application with
  // --magnum-gpu-validation on
  auto error = GL::Renderer::error();
  if (error == GL::Renderer::Error::InvalidFramebufferOperation)
    pc::logger->warn("InvalidFramebufferOperation");
  if (error == GL::Renderer::Error::InvalidOperation)
    pc::logger->warn("InvalidOperation");
  if (error == GL::Renderer::Error::InvalidValue)
    pc::logger->warn("InvalidValue");
  if (error == GL::Renderer::Error::StackOverflow)
    pc::logger->warn("StackOverflow");
  if (error == GL::Renderer::Error::StackUnderflow)
    pc::logger->warn("StackUnderflow");


  // auto& camera_controller = _camera_controllers.at(0);
  // GL::Texture2D& frame = camera_controller->color_frame();

  // _secondary_window->_shader.bind_texture(frame).draw(_secondary_window->_mesh);

  if (_secondary_window.has_value()) _secondary_window->redraw();

  redraw();


  // _secondary_window->redraw();

  swapBuffers();

  parameters::publish();

  _timeline.nextFrame();
  FrameMark;
}

void CameraDisplayWindow::drawEvent() {
  GL::defaultFramebuffer.clear(GL::FramebufferClear::Color |
			       GL::FramebufferClear::Depth);

  auto& pc = application();

  auto& camera_controller = pc._camera_controllers.at(0);
  GL::Texture2D& frame = camera_controller->color_frame();

  _shader.bind_texture(frame).draw(_mesh);

  swapBuffers();
}

void PointCaster::set_full_screen(bool full_screen) {
  if (full_screen && _display_resolution.has_value()) {
    pc::logger->info("going full screen");
    if (!_full_screen) {
      _restore_window_size = windowSize() / dpiScaling();
      SDL_GetWindowPosition(window(), &_restore_window_position.x(), &_restore_window_position.y());
    }
    setWindowSize(_display_resolution.value() / dpiScaling());
    SDL_SetWindowPosition(window(), 0, 0);
    _full_screen = true;
  } else if (!full_screen) {
    pc::logger->info("restoring out");
    setWindowSize(_restore_window_size);
    SDL_SetWindowPosition(window(), _restore_window_position.x(), _restore_window_position.y());
    _full_screen = false;
  }
}

void PointCaster::viewportEvent(ViewportEvent &event) {
  // resize main framebuffer
  GL::defaultFramebuffer.setViewport({{}, event.framebufferSize()});

  // relayout imgui
  _imgui_context.relayout(Vector2{event.windowSize()} / event.dpiScaling(),
                          event.windowSize(), event.framebufferSize());

  // recompute the camera's projection matrix
  // camera->setViewport(event.framebufferSize());
}


void CameraDisplayWindow::viewportEvent(ViewportEvent &event) {
  GL::defaultFramebuffer.setViewport({{}, event.framebufferSize()});
}

void PointCaster::keyPressEvent(KeyEvent &event) {

  if (_current_mode == Mode::Find) {
    find_mode_keypress(event);
    if (_imgui_context.handleKeyPressEvent(event))
      event.setAccepted(true);
    return;
  }
  if (_current_mode != Mode::Normal && event.key() == KeyEvent::Key::Esc) {
    _current_mode = Mode::Normal;
    return;
  } else if (_current_mode == Mode::Normal &&
	     event.key() == KeyEvent::Key::Slash) {
    _current_mode = Mode::Find;
    return;
  }

  switch (event.key()) {
  case KeyEvent::Key::C: {
    CameraController &active_camera_controller =
	_interacting_camera_controller ? _interacting_camera_controller->get()
				       : *_camera_controllers[0];
    bool showing_window = active_camera_controller.config().show_window;
    active_camera_controller.config().show_window = !showing_window;
    break;
  }
  case KeyEvent::Key::D: {
    _session.layout.show_devices_window = !_session.layout.show_devices_window;
    break;
  }
  case KeyEvent::Key::F: {
    set_full_screen(!_full_screen);
    break;
  }
  case KeyEvent::Key::G: {
    if (event.modifiers() == InputEvent::Modifier::Shift) {
      _session.layout.hide_ui = !_session.layout.hide_ui;
    } else {
      _session.layout.show_global_transform_window =
          !_session.layout.show_global_transform_window;
    }
    break;
  }
  case KeyEvent::Key::M: {
    if (event.modifiers() == InputEvent::Modifier::Shift) {
      if (_session.mqtt.has_value()) {
	auto& mqtt_conf = _session.mqtt.value();
        mqtt_conf.show_window = !mqtt_conf.show_window;
      }
    } else {
      if (_session.midi.has_value()) {
	auto& midi_conf = _session.midi.value();
        midi_conf.show_window = !midi_conf.show_window;
      }
    }
    break;
  }
  case KeyEvent::Key::O: {
    if (event.modifiers() == InputEvent::Modifier::Shift) {
      if (_session.osc_client.has_value()) {
        auto &osc_client_conf = _session.osc_client.value();
        osc_client_conf.show_window = !osc_client_conf.show_window;
      }
    } else {
      if (_session.osc_server.has_value()) {
        auto &osc_server_conf = _session.osc_server.value();
        osc_server_conf.show_window = !osc_server_conf.show_window;
      }
    }
    break;
  }
  case KeyEvent::Key::Q: {
    quit();
    break;
  }
  case KeyEvent::Key::R: {
    _session.layout.show_radio_window = !_session.layout.show_radio_window;
    break;
  }
  case KeyEvent::Key::S: {
    if (event.modifiers() == InputEvent::Modifier::Shift) {
      _session.layout.show_snapshots_window =
	  !_session.layout.show_snapshots_window;
    } else {
      save_session();
    }
    break;
  }
  case KeyEvent::Key::T: {
    _session.layout.show_stats = !_session.layout.show_stats;
    break;
  }
  default: {
    if (_imgui_context.handleKeyPressEvent(event))
      event.setAccepted(true);
  }
  }
}

void PointCaster::keyReleaseEvent(KeyEvent &event) {
  if (_imgui_context.handleKeyReleaseEvent(event))
    event.setAccepted(true);
}

void PointCaster::textInputEvent(TextInputEvent &event) {
  if (_imgui_context.handleTextInputEvent(event))
    event.setAccepted(true);
}

void PointCaster::mousePressEvent(MouseEvent &event) {
  if (_imgui_context.handleMousePressEvent(event)) {
    event.setAccepted(true);
    return;
  }
}

void PointCaster::mouseReleaseEvent(MouseEvent &event) {
  if (_imgui_context.handleMouseReleaseEvent(event)) {
    event.setAccepted(true);
  }
}

// TODO
// all camera related mouse events need to happen on top of the selected camera
// window

void PointCaster::mouseMoveEvent(MouseMoveEvent &event) {
  if (_imgui_context.handleMouseMoveEvent(event) &&
      !_interacting_camera_controller) {
    event.setAccepted(true);
    return;
  }

  if (!_interacting_camera_controller) {
	  event.setAccepted(true);
	  return;
  }

  auto& camera_controller = _interacting_camera_controller->get();

  // rotate / orbit
  if (event.buttons() == MouseMoveEvent::Button::Left) {
    camera_controller.mouse_orbit(event);
  }
  // translate
  else if (event.buttons() == MouseMoveEvent::Button::Right) {
    auto lock_y_axis = event.modifiers() == InputEvent::Modifier::Shift;
    camera_controller.mouse_translate(event, lock_y_axis);
  }

  event.setAccepted();
}

void PointCaster::mouseScrollEvent(MouseScrollEvent &event) {
  if (_imgui_context.handleMouseScrollEvent(event) &&
      !_interacting_camera_controller) {
    /* Prevent scrolling the page */
    event.setAccepted(true);
    return;
  }

  auto& camera_controller = _interacting_camera_controller->get();

  const Magnum::Float delta = event.offset().y();
  if (Math::abs(delta) < 1.0e-2f) return;

  camera_controller.dolly(event);
}

} // namespace pc

MAGNUM_APPLICATION_MAIN(pc::PointCaster);
