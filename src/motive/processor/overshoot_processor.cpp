// Copyright 2014 Google Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "motive/engine.h"
#include "motive/init.h"

namespace motive {

struct OvershootData {
  void Initialize(const OvershootInit& init_param) {
    value = 0.0f;
    velocity = 0.0f;
    target_value = 0.0f;
    init = init_param;
  }

  // What we are animating. Returned when Motivator::Value() called.
  float value;

  // The rate of change of value. Returned when Motivator::Velocity() called.
  float velocity;

  // What we are striving to hit. Returned when Motivator::TargetValue() called.
  float target_value;

  // Keep a local copy of the init params.
  OvershootInit init;
};

class OvershootMotiveProcessor : public MotiveProcessorVector {
 public:
  virtual ~OvershootMotiveProcessor() {}

  virtual void AdvanceFrame(MotiveTime delta_time) {
    Defragment();

    // Loop through every motivator one at a time.
    // TODO: change this to a closed-form equation.
    // TODO OPT: reorder data and then optimize with SIMD to process in groups
    // of 4 floating-point or 8 fixed-point values.
    for (auto d = data_.begin(); d < data_.end(); ++d) {
      for (MotiveTime time_remaining = delta_time; time_remaining > 0;) {
        MotiveTime dt = std::min(time_remaining, d->init.max_delta_time());

        d->velocity = CalculateVelocity(dt, *d);
        d->value = CalculateValue(dt, *d);

        time_remaining -= dt;
      }
    }
  }

  virtual MotivatorType Type() const { return OvershootInit::kType; }
  virtual int Priority() const { return 1; }

  // Accessors to allow the user to get and set simluation values.
  virtual float Value1f(MotiveIndex index) const { return Data(index).value; }
  virtual float Velocity1f(MotiveIndex index) const {
    return Data(index).velocity;
  }
  virtual float TargetValue1f(MotiveIndex index) const {
    return Data(index).target_value;
  }
  virtual float TargetVelocity1f(MotiveIndex /*index*/) const { return 0.0f; }
  virtual float Difference1f(MotiveIndex index) const {
    const OvershootData& d = Data(index);
    return d.init.Normalize(d.target_value - d.value);
  }
  // TODO: Implement this after converting Overshoot to use splines.
  virtual MotiveTime TargetTime(MotiveIndex /*index*/) const {
    assert(false);
    return -1;
  }
  virtual void SetTarget(MotiveIndex index, const MotiveTarget1f& t) {
    OvershootData& d = Data(index);

    // A 'time' of 0 means that we're setting the current values.
    const MotiveNode1f& current = t.Node(0);
    if (current.time == 0) {
      d.value = current.value;
      d.velocity = current.velocity;
    }

    // A 'time' > 0 means that we're setting the target values.
    // We can also use the second node to set target values, if it exists.
    const MotiveNode1f* target =
        current.time == 0 ? (t.num_nodes() > 1 ? &t.Node(1) : nullptr)
                          : &t.Node(0);
    if (target != nullptr) {
      d.target_value = target->value;
    }
  }

 protected:
  virtual void InitializeIndex(const MotivatorInit& init, MotiveIndex index,
                               MotiveEngine* engine) {
    (void)engine;
    Data(index).Initialize(static_cast<const OvershootInit&>(init));
  }

  virtual void RemoveIndex(MotiveIndex index) {
    Data(index).Initialize(OvershootInit());
  }

  virtual void MoveIndex(MotiveIndex old_index, MotiveIndex new_index) {
    data_[new_index] = data_[old_index];
  }

  virtual void SetNumIndices(MotiveIndex num_indices) {
    data_.resize(num_indices);
  }

  const OvershootData& Data(MotiveIndex index) const {
    assert(ValidIndex(index));
    return data_[index];
  }

  OvershootData& Data(MotiveIndex index) {
    assert(ValidIndex(index));
    return data_[index];
  }

  float CalculateVelocity(MotiveTime delta_time, const OvershootData& d) const {
    // Increment our current face angle velocity.
    // If we're moving in the wrong direction (i.e. away from the target),
    // increase the acceleration. This results in us moving towards the target
    // for longer time than we move away from the target, or equivalently,
    // aggressively initiating our movement towards the target, which feels
    // good.
    const float diff = d.init.Normalize(d.target_value - d.value);
    const bool wrong_direction = d.velocity * diff < 0.0f;
    const float wrong_direction_multiplier =
        wrong_direction ? d.init.wrong_direction_multiplier() : 1.0f;
    const float acceleration =
        diff * d.init.accel_per_difference() * wrong_direction_multiplier;
    const float velocity_unclamped = d.velocity + delta_time * acceleration;

    // Always ensure the velocity remains within the valid limits.
    const float velocity = d.init.ClampVelocity(velocity_unclamped);

    // If we're far from facing the target, use the velocity calculated above.
    const bool should_snap = d.init.AtTarget(diff, velocity);
    if (should_snap) return 0.0f;

    return velocity;
  }

  float CalculateValue(MotiveTime delta_time, const OvershootData& d) const {
    // Snap to the target value when we've stopped moving.
    if (d.velocity == 0.0f) return d.target_value;

    const float delta = d.init.ClampDelta(delta_time * d.velocity);
    const float value_unclamped = d.init.Normalize(d.value + delta);
    const float value = d.init.ClampValue(value_unclamped);
    return value;
  }

  std::vector<OvershootData> data_;
};

MOTIVE_INSTANCE(OvershootInit, OvershootMotiveProcessor);

}  // namespace motive
