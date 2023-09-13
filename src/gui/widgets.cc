#include "widgets.h"
#include <spdlog/spdlog.h>
#include <tweeny/easing.h>

namespace pc::gui {

unsigned int _parameter_index;

void begin_gui_helpers() { _parameter_index = 0; }

template <typename T>
void draw_slider(std::string_view label_text, T *value, T min, T max,
                 T default_value) {
  ImGui::PushID(_parameter_index++);
  ImGui::Text("%s", label_text.data());
  ImGui::SameLine();

  constexpr auto parameter = [](auto text) constexpr {
    return std::string("##") + std::string(text);
  };
  if constexpr (std::is_integral<T>())
    ImGui::SliderInt(parameter(label_text).c_str(), value, min, max);
  else if constexpr (std::is_floating_point<T>())
    ImGui::SliderFloat(parameter(label_text).c_str(), value, min, max, "%.5g");

  ImGui::SameLine();
  if (ImGui::Button("0"))
    *value = default_value;
  ImGui::PopID();
}

template void draw_slider<int>(std::string_view, int *, int, int, int);
template void draw_slider<float>(std::string_view, float *, float, float,
                                 float);

template <typename T>
void slider(const std::string &parameter_id, std::size_t i, T &value, T min, T max,
            T reset_value, bool is_disabled, const std::string &label_dimension,
            const std::string &base_label) {
  if (is_disabled)
    ImGui::BeginDisabled();

  auto &state = parameter_states[parameter_id];
  auto new_state = state;

  // set the slider value by it's binding value if it exists
  if (state == ParameterState::Bound) {
    value = parameter_bindings[parameter_id].value;
    // and colour it purple
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.7f, 0.4f, 0.7f, 0.25f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.7f, 0.4f, 0.7f, 0.35f});
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, {0.7f, 0.4f, 0.7f, 0.9f});
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, {0.7f, 0.4f, 0.7f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.7f, 0.4f, 0.7f, 0.25f});
  }
  else if (state == ParameterState::Recording) {
    // colour it red for recording
    ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.7f, 0.4f, 0.4f, 0.25f});
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, {0.7f, 0.4f, 0.4f, 0.35f});
    ImGui::PushStyleColor(ImGuiCol_SliderGrab, {0.7f, 0.4f, 0.4f, 0.9f});
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, {0.7f, 0.4f, 0.4f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_Button, {0.7f, 0.4f, 0.4f, 0.25f});
    // if we *were* recording this slider and we're not anymore,
    // set its status
    if (!recording_parameter) new_state = recording_result;
  }

  // Label Column
  ImGui::TableSetColumnIndex(0);
  ImGui::Text(label_dimension.c_str());

  // Slider Column
  ImGui::TableSetColumnIndex(1);
  ImGui::SetNextItemWidth(-1);

  if (state != ParameterState::Bound) {
    if constexpr (std::is_same_v<T, float>) {
      ImGui::SliderFloat(parameter_id.c_str(), &value, min, max);
    } else if constexpr (std::is_same_v<T, int>) {
      ImGui::SliderInt(parameter_id.c_str(), &value, min, max);
    }
  } else {
    // if the slider is bound, draw a range slider to set the min and max values
    ImGui::SetNextItemWidth(-1);
    auto &binding = parameter_bindings[parameter_id];
    auto old_binding = binding;
    if (ImGui::RangeSliderFloat(("##" + parameter_id + ".minmax").c_str(),
                                &binding.min, &binding.max, min, max)) {
      // and if the range is updated, make sure to trigger update callbacks
      for (const auto &cb : binding.minmax_update_callbacks) {
        cb(old_binding, binding);
      }
    }
  }

  // Right click toggles controller learning
  if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
    if (!recording_parameter) {
      // if we were not recording, set it to record
      new_state = ParameterState::Recording;
      recording_parameter = true;
      store_recording_parameter_info(parameter_id, min, max);
    } else {
      // if we were recording, return the slider to an unbound state
      new_state = ParameterState::Unbound;
      recording_parameter = false;
    }
    // in both cases we remove any existing binding
    if (parameter_bindings.find(parameter_id) != parameter_bindings.end()) {
      parameter_bindings.erase(parameter_id);
    }
  }

  // Reset Button Column
  ImGui::TableSetColumnIndex(2);
  if (state == ParameterState::Bound) ImGui::BeginDisabled();
  if (ImGui::Button("·", {15, 18})) {
    value = reset_value;
  }
  if (state == ParameterState::Bound) ImGui::EndDisabled();

  ImGui::PopStyleColor(state != ParameterState::Unbound ? 5 : 0);

  if (is_disabled) ImGui::EndDisabled();

  state = new_state;
}

template void slider(const std::string &parameter_id, std::size_t i, float &value,
                     float min, float max, float reset_value, bool is_disabled,
                     const std::string &label_dimension,
                     const std::string &base_label);

template void slider(const std::string &parameter_id, std::size_t i, int &value,
                     int min, int max, int reset_value, bool is_disabled,
                     const std::string &label_dimension,
                     const std::string &base_label);

template <typename T, std::size_t N>
bool vector_table(const std::string group_id, const std::string label,
                  std::array<T, N> &vec, T min, T max,
                  std::array<T, N> reset_values, std::array<bool, N> disabled,
                  std::array<std::string, N> labels) {

  static_assert(N >= 2 && N <= 4, "Vector array must have 2, 3 or 4 elements");

  constexpr auto outer_horizontal_padding = 4;
  constexpr auto table_background_color = IM_COL32(22, 27, 34, 255);

  const char *dimensions[] = {"x", "y", "z", "w"};

  auto row_height = ImGui::GetTextLineHeightWithSpacing() * 1.33f;
  auto table_height = (row_height * (N + 1)) + 14;

  ImGui::PushID(_parameter_index++);

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      {outer_horizontal_padding, 0});
  ImGui::PushStyleColor(ImGuiCol_ChildBg, table_background_color);
  ImGui::BeginChild("##vector_table_border", {0, table_height}, true,
                    ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_NoScrollbar);
  ImGui::Dummy({0, outer_horizontal_padding});
  ImGui::Dummy({outer_horizontal_padding, 0});
  ImGui::SameLine(0, 0);
  ImGui::Text(label.c_str());
  ImGui::Dummy({0, 0});

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {0, 2});
  ImGui::BeginTable(label.c_str(), 3, ImGuiTableFlags_SizingFixedFit);
  ImGui::TableSetupColumn("##label", ImGuiTableColumnFlags_WidthFixed);
  ImGui::TableSetupColumn("##slider", ImGuiTableColumnFlags_WidthStretch);
  ImGui::TableSetupColumn("##reset_button", ImGuiTableColumnFlags_WidthFixed);

  auto original_vec = vec;

  auto use_labels = !labels.at(0).empty();

  for (std::size_t i = 0; i < N; ++i) {
    const auto &row_label = use_labels ? labels[i] : dimensions[i];
    ImGui::TableNextRow();
    slider(group_id + "." + label + "." + row_label, i, vec[i], min, max,
           reset_values[i], disabled[i], row_label, label);
  }

  ImGui::EndTable();
  ImGui::PopStyleVar();

  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();

  ImGui::PopID();

  return vec != original_vec;
}

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<float, 2> &vec, float min, float max,
                           std::array<float, 2> reset_values,
                           std::array<bool, 2> disabled,
                           std::array<std::string, 2> labels);

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<float, 3> &vec, float min, float max,
                           std::array<float, 3> reset_values,
                           std::array<bool, 3> disabled,
                           std::array<std::string, 3> labels);

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<float, 4> &vec, float min, float max,
                           std::array<float, 4> reset_values,
                           std::array<bool, 4> disabled,
                           std::array<std::string, 4> labels);

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<int, 2> &vec, int min, int max,
                           std::array<int, 2> reset_values,
                           std::array<bool, 2> disabled,
                           std::array<std::string, 2> labels);

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<int, 3> &vec, int min, int max,
                           std::array<int, 3> reset_values,
                           std::array<bool, 3> disabled,
                           std::array<std::string, 3> labels);

template bool vector_table(const std::string group_id, const std::string label,
                           std::array<int, 4> &vec, int min, int max,
                           std::array<int, 4> reset_values,
                           std::array<bool, 4> disabled,
                           std::array<std::string, 4> labels);

bool begin_tree_node(std::string_view name, bool &open) {
  ImGui::PushID(_parameter_index++);
  auto node_flags = ImGuiTreeNodeFlags_None;
  if (open)
    node_flags = ImGuiTreeNodeFlags_DefaultOpen;
  open = ImGui::TreeNodeEx(name.data(), node_flags);
  ImGui::PopID();
  return open;
}

void tween_config(std::string_view label,
                  pc::tween::TweenConfiguration &config) {
  ImGui::PushID(_parameter_index++);

  gui::draw_slider("Duration (ms)", &config.duration_ms, 0, 2000, 300);

  static const std::map<tweeny::easing::enumerated, std::pair<int, std::string>>
      ease_function_to_combo_item = {
          {tweeny::easing::enumerated::def, {0, "Default"}},
          {tweeny::easing::enumerated::linear, {1, "Linear"}},
          {tweeny::easing::enumerated::stepped, {2, "Stepped"}},
          {tweeny::easing::enumerated::quadraticIn, {3, "Quadratic In"}},
          {tweeny::easing::enumerated::quadraticOut, {4, "Quadratic Out"}},
          {tweeny::easing::enumerated::quadraticInOut, {5, "Quadratic InOut"}},
          {tweeny::easing::enumerated::cubicIn, {6, "Cubic In"}},
          {tweeny::easing::enumerated::cubicOut, {7, "Cubic Out"}},
          {tweeny::easing::enumerated::cubicInOut, {8, "Cubic InOut"}},
          {tweeny::easing::enumerated::quarticIn, {9, "Quartic In"}},
          {tweeny::easing::enumerated::quarticOut, {10, "Quartic Out"}},
          {tweeny::easing::enumerated::quarticInOut, {11, "Quartic InOut"}},
          {tweeny::easing::enumerated::quinticIn, {12, "Quintic In"}},
          {tweeny::easing::enumerated::quinticOut, {13, "Quintic Out"}},
          {tweeny::easing::enumerated::quinticInOut, {14, "Quintic InOut"}},
          {tweeny::easing::enumerated::sinusoidalIn, {15, "Sinusoidal In"}},
          {tweeny::easing::enumerated::sinusoidalOut, {16, "Sinusoidal Out"}},
          {tweeny::easing::enumerated::sinusoidalInOut,
           {17, "Sinusoidal InOut"}},
          {tweeny::easing::enumerated::exponentialIn, {18, "Exponential In"}},
          {tweeny::easing::enumerated::exponentialOut, {19, "Exponential Out"}},
          {tweeny::easing::enumerated::exponentialInOut,
           {20, "Exponential InOut"}},
          {tweeny::easing::enumerated::circularIn, {21, "Circular In"}},
          {tweeny::easing::enumerated::circularOut, {22, "Circular Out"}},
          {tweeny::easing::enumerated::circularInOut, {23, "Circular InOut"}},
          {tweeny::easing::enumerated::bounceIn, {24, "Bounce In"}},
          {tweeny::easing::enumerated::bounceOut, {25, "Bounce Out"}},
          {tweeny::easing::enumerated::bounceInOut, {26, "Bounce InOut"}},
          {tweeny::easing::enumerated::elasticIn, {27, "Elastic In"}},
          {tweeny::easing::enumerated::elasticOut, {28, "Elastic Out"}},
          {tweeny::easing::enumerated::elasticInOut, {29, "Elastic InOut"}},
          {tweeny::easing::enumerated::backIn, {30, "Back In"}},
          {tweeny::easing::enumerated::backOut, {31, "Back Out"}},
          {tweeny::easing::enumerated::backInOut, {32, "Back InOut"}}};

  static const std::string combo_item_string = [] {
    std::string items;
    for (const auto &[mode, item] : ease_function_to_combo_item) {
      items += item.second + '\0';
    }
    return items;
  }();

  auto [selected_item_index, item_label] =
      ease_function_to_combo_item.at(config.ease_function);

  if (ImGui::Combo(std::string("##" + item_label).c_str(), &selected_item_index,
                   combo_item_string.c_str())) {
    for (const auto &[ease_function, combo_item] :
         ease_function_to_combo_item) {
      if (combo_item.first == selected_item_index) {
        config.ease_function = ease_function;
        break;
      }
    }
  }

  ImGui::PopID();
}

} // namespace pc::gui
