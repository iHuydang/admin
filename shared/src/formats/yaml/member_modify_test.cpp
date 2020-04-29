#include <gtest/gtest.h>

#include <formats/yaml/exception.hpp>
#include <formats/yaml/serialize.hpp>
#include <formats/yaml/value_builder.hpp>

#include <formats/common/member_modify_test.hpp>

namespace {
constexpr const char* kDoc = R"(
key1: 1
key2: val
key3:
  sub: -1
key4: [1, 2, 3]
key5: 10.5
)";
}

template <>
struct MemberModify<formats::yaml::Value> : public ::testing::Test {
  MemberModify() : builder_(formats::yaml::FromString(kDoc)) {}

  formats::yaml::Value GetValue(formats::yaml::ValueBuilder& bld) {
    auto v = bld.ExtractValue();
    bld = v;
    return v;
  }

  formats::yaml::Value GetBuiltValue() { return GetValue(builder_); }

  formats::yaml::ValueBuilder builder_;

  using ValueBuilder = formats::yaml::ValueBuilder;
  using Value = formats::yaml::Value;
  using Type = formats::yaml::Type;

  using ParseException = formats::yaml::ParseException;
  using TypeMismatchException = formats::yaml::TypeMismatchException;
  using OutOfBoundsException = formats::yaml::OutOfBoundsException;
  using MemberMissingException = formats::yaml::MemberMissingException;
  using Exception = formats::yaml::Exception;

  constexpr static auto FromString = formats::yaml::FromString;
};

INSTANTIATE_TYPED_TEST_SUITE_P(FormatsYaml, MemberModify, formats::yaml::Value);
