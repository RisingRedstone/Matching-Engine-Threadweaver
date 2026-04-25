#pragma once

#include <concepts>
namespace engine::buffer::roles {
struct SingleInstance {};
struct MultipleInstance {};
template <typename Role>
concept IsMultipleInstanceConsumer = requires {
  typename Role::ConsumerInstanceType;
  requires std::same_as<typename Role::ConsumerInstanceType, MultipleInstance>;
};
template <typename Role>
concept IsSingleInstanceConsumer = requires {
  typename Role::ConsumerInstanceType;
  requires std::same_as<typename Role::ConsumerInstanceType, SingleInstance> ||
               std::same_as<typename Role::ConsumerInstanceType,
                            MultipleInstance>;
};
template <typename Role>
concept IsMultipleInstanceProducer = requires {
  typename Role::ProducerInstanceType;
  requires std::same_as<typename Role::ProducerInstanceType, MultipleInstance>;
};
template <typename Role>
concept IsSingleInstanceProducer = requires {
  typename Role::ProducerInstanceType;
  requires std::same_as<typename Role::ProducerInstanceType, SingleInstance> ||
               std::same_as<typename Role::ProducerInstanceType,
                            MultipleInstance>;
};
} // namespace engine::buffer::roles
