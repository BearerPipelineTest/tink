// Copyright 2021 Google LLC
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
//
////////////////////////////////////////////////////////////////////////////////

#include "tink/jwt/internal/jwt_mac_wrapper.h"

#include "gtest/gtest.h"
#include "absl/strings/str_split.h"
#include "tink/jwt/internal/json_util.h"
#include "tink/jwt/internal/jwt_format.h"
#include "tink/jwt/internal/jwt_hmac_key_manager.h"
#include "tink/keyset_manager.h"
#include "tink/primitive_set.h"
#include "tink/util/status.h"
#include "tink/util/test_matchers.h"
#include "tink/util/test_util.h"
#include "proto/jwt_hmac.pb.h"
#include "proto/tink.pb.h"

using google::crypto::tink::JwtHmacAlgorithm;
using google::crypto::tink::JwtHmacKeyFormat;
using google::crypto::tink::KeyTemplate;
using google::crypto::tink::OutputPrefixType;

namespace crypto {
namespace tink {
namespace jwt_internal {
namespace {

using ::crypto::tink::test::IsOk;
using ::testing::Eq;

KeyTemplate createTemplate(OutputPrefixType output_prefix) {
  KeyTemplate key_template;
  key_template.set_type_url(
      "type.googleapis.com/google.crypto.tink.JwtHmacKey");
  key_template.set_output_prefix_type(output_prefix);
  JwtHmacKeyFormat key_format;
  key_format.set_key_size(32);
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);
  key_format.SerializeToString(key_template.mutable_value());
  return key_template;
}

class JwtMacWrapperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ASSERT_THAT(
        Registry::RegisterPrimitiveWrapper(absl::make_unique<JwtMacWrapper>()),
        IsOk());
    ASSERT_THAT(Registry::RegisterKeyTypeManager(
                    absl::make_unique<JwtHmacKeyManager>(), true),
                IsOk());
  }
};

TEST_F(JwtMacWrapperTest, WrapNullptr) {
  auto mac_result = JwtMacWrapper().Wrap(nullptr);
  EXPECT_FALSE(mac_result.ok());
}

TEST_F(JwtMacWrapperTest, WrapEmpty) {
  auto jwt_mac_set = absl::make_unique<PrimitiveSet<JwtMacInternal>>();
  auto jwt_mac_result = JwtMacWrapper().Wrap(std::move(jwt_mac_set));
  EXPECT_FALSE(jwt_mac_result.ok());
}

TEST_F(JwtMacWrapperTest, CannotWrapPrimitivesFromNonRawOrTinkKeys) {
  KeyTemplate tink_key_template = createTemplate(OutputPrefixType::LEGACY);

  auto handle_result = KeysetHandle::GenerateNew(tink_key_template);
  EXPECT_THAT(handle_result.status(), IsOk());
  auto keyset_handle = std::move(handle_result.ValueOrDie());

  EXPECT_FALSE(keyset_handle->GetPrimitive<JwtMac>().status().ok());
}

TEST_F(JwtMacWrapperTest, GenerateRawComputeVerifySuccess) {
  KeyTemplate key_template = createTemplate(OutputPrefixType::RAW);
  auto handle_result = KeysetHandle::GenerateNew(key_template);
  EXPECT_THAT(handle_result.status(), IsOk());
  auto keyset_handle = std::move(handle_result.ValueOrDie());
  auto jwt_mac_or = keyset_handle->GetPrimitive<JwtMac>();
  EXPECT_THAT(jwt_mac_or.status(), IsOk());
  std::unique_ptr<JwtMac> jwt_mac = std::move(jwt_mac_or.ValueOrDie());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  RawJwt raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_mac->ComputeMacAndEncode(raw_jwt);
  ASSERT_THAT(compact_or.status(), IsOk());
  std::string compact = compact_or.ValueOrDie();

  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectIssuer("issuer")
                               .AllowMissingExpiration()
                               .Build()
                               .ValueOrDie();
  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_mac->VerifyMacAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  auto verified_jwt = verified_jwt_or.ValueOrDie();
  EXPECT_THAT(verified_jwt.GetIssuer(), test::IsOkAndHolds("issuer"));

  JwtValidator validator2 = JwtValidatorBuilder()
                                .ExpectIssuer("unknown")
                                .AllowMissingExpiration()
                                .Build()
                                .ValueOrDie();
  util::StatusOr<VerifiedJwt> verified_jwt_or2 =
      jwt_mac->VerifyMacAndDecode(compact, validator2);
  EXPECT_FALSE(verified_jwt_or2.ok());
  // Make sure the error message is interesting
  EXPECT_THAT(verified_jwt_or2.status().error_message(), Eq("wrong issuer"));
}

TEST_F(JwtMacWrapperTest, GenerateTinkComputeVerifySuccess) {
  KeyTemplate key_template = createTemplate(OutputPrefixType::TINK);
  auto handle_result = KeysetHandle::GenerateNew(key_template);
  EXPECT_THAT(handle_result.status(), IsOk());
  auto keyset_handle = std::move(handle_result.ValueOrDie());
  auto jwt_mac_or = keyset_handle->GetPrimitive<JwtMac>();
  EXPECT_THAT(jwt_mac_or.status(), IsOk());
  std::unique_ptr<JwtMac> jwt_mac = std::move(jwt_mac_or.ValueOrDie());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  RawJwt raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_mac->ComputeMacAndEncode(raw_jwt);
  ASSERT_THAT(compact_or.status(), IsOk());
  std::string compact = compact_or.ValueOrDie();

  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectIssuer("issuer")
                               .AllowMissingExpiration()
                               .Build()
                               .ValueOrDie();
  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_mac->VerifyMacAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  auto verified_jwt = verified_jwt_or.ValueOrDie();
  EXPECT_THAT(verified_jwt.GetIssuer(), test::IsOkAndHolds("issuer"));

  // parse header to make sure that key ID is correctly encoded.
  auto keyset_info = keyset_handle->GetKeysetInfo();
  uint32_t key_id = keyset_info.key_info(0).key_id();
  std::vector<absl::string_view> parts =
      absl::StrSplit(compact_or.ValueOrDie(), '.');
  ASSERT_THAT(parts.size(), Eq(3));
  std::string json_header;
  ASSERT_TRUE(DecodeHeader(parts[0], &json_header));
  auto header_or = JsonStringToProtoStruct(json_header);
  ASSERT_THAT(header_or.status(), IsOk());
  EXPECT_THAT(
      GetKeyId(
          header_or.ValueOrDie().fields().find("kid")->second.string_value()),
      key_id);
}

TEST_F(JwtMacWrapperTest, KeyRotation) {
  std::vector<OutputPrefixType> prefixes = {OutputPrefixType::RAW,
                                            OutputPrefixType::TINK};
  for (OutputPrefixType prefix : prefixes) {
    SCOPED_TRACE(absl::StrCat("Testing with prefix ", prefix));
    KeyTemplate key_template = createTemplate(prefix);
    KeysetManager manager;

    auto old_id_or = manager.Add(key_template);
    ASSERT_THAT(old_id_or.status(), IsOk());
    uint32_t old_id = old_id_or.ValueOrDie();
    ASSERT_THAT(manager.SetPrimary(old_id), IsOk());
    std::unique_ptr<KeysetHandle> handle1 = manager.GetKeysetHandle();
    auto jwt_mac1_or = handle1->GetPrimitive<JwtMac>();
    ASSERT_THAT(jwt_mac1_or.status(), IsOk());
    std::unique_ptr<JwtMac> jwt_mac1 = std::move(jwt_mac1_or.ValueOrDie());

    auto new_id_or = manager.Add(key_template);
    ASSERT_THAT(new_id_or.status(), IsOk());
    uint32_t new_id = new_id_or.ValueOrDie();
    std::unique_ptr<KeysetHandle> handle2 = manager.GetKeysetHandle();
    auto jwt_mac2_or = handle2->GetPrimitive<JwtMac>();
    ASSERT_THAT(jwt_mac2_or.status(), IsOk());
    std::unique_ptr<JwtMac> jwt_mac2 = std::move(jwt_mac2_or.ValueOrDie());

    ASSERT_THAT(manager.SetPrimary(new_id), IsOk());
    std::unique_ptr<KeysetHandle> handle3 = manager.GetKeysetHandle();
    auto jwt_mac3_or = handle3->GetPrimitive<JwtMac>();
    ASSERT_THAT(jwt_mac3_or.status(), IsOk());
    std::unique_ptr<JwtMac> jwt_mac3 = std::move(jwt_mac3_or.ValueOrDie());

    ASSERT_THAT(manager.Disable(old_id), IsOk());
    std::unique_ptr<KeysetHandle> handle4 = manager.GetKeysetHandle();
    auto jwt_mac4_or = handle4->GetPrimitive<JwtMac>();
    ASSERT_THAT(jwt_mac4_or.status(), IsOk());
    std::unique_ptr<JwtMac> jwt_mac4 = std::move(jwt_mac4_or.ValueOrDie());

    auto raw_jwt_or =
        RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
    ASSERT_THAT(raw_jwt_or.status(), IsOk());
    RawJwt raw_jwt = raw_jwt_or.ValueOrDie();
    JwtValidator validator = JwtValidatorBuilder()
                                 .ExpectIssuer("issuer")
                                 .AllowMissingExpiration()
                                 .Build()
                                 .ValueOrDie();

    util::StatusOr<std::string> compact1_or =
        jwt_mac1->ComputeMacAndEncode(raw_jwt);
    ASSERT_THAT(compact1_or.status(), IsOk());
    std::string compact1 = compact1_or.ValueOrDie();

    util::StatusOr<std::string> compact2_or =
        jwt_mac2->ComputeMacAndEncode(raw_jwt);
    ASSERT_THAT(compact2_or.status(), IsOk());
    std::string compact2 = compact2_or.ValueOrDie();

    util::StatusOr<std::string> compact3_or =
        jwt_mac3->ComputeMacAndEncode(raw_jwt);
    ASSERT_THAT(compact3_or.status(), IsOk());
    std::string compact3 = compact3_or.ValueOrDie();

    util::StatusOr<std::string> compact4_or =
        jwt_mac4->ComputeMacAndEncode(raw_jwt);
    ASSERT_THAT(compact4_or.status(), IsOk());
    std::string compact4 = compact4_or.ValueOrDie();

    EXPECT_THAT(jwt_mac1->VerifyMacAndDecode(compact1, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac2->VerifyMacAndDecode(compact1, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac3->VerifyMacAndDecode(compact1, validator).status(),
                IsOk());
    EXPECT_FALSE(jwt_mac4->VerifyMacAndDecode(compact1, validator).ok());

    EXPECT_THAT(jwt_mac1->VerifyMacAndDecode(compact2, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac2->VerifyMacAndDecode(compact2, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac3->VerifyMacAndDecode(compact2, validator).status(),
                IsOk());
    EXPECT_FALSE(jwt_mac4->VerifyMacAndDecode(compact2, validator).ok());

    EXPECT_FALSE(jwt_mac1->VerifyMacAndDecode(compact3, validator).ok());
    EXPECT_THAT(jwt_mac2->VerifyMacAndDecode(compact3, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac3->VerifyMacAndDecode(compact3, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac4->VerifyMacAndDecode(compact3, validator).status(),
                IsOk());

    EXPECT_FALSE(jwt_mac1->VerifyMacAndDecode(compact4, validator).ok());
    EXPECT_THAT(jwt_mac2->VerifyMacAndDecode(compact4, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac3->VerifyMacAndDecode(compact4, validator).status(),
                IsOk());
    EXPECT_THAT(jwt_mac4->VerifyMacAndDecode(compact4, validator).status(),
                IsOk());
  }
}

}  // namespace
}  // namespace jwt_internal
}  // namespace tink
}  // namespace crypto
