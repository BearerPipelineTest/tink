// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
////////////////////////////////////////////////////////////////////////////////

#include "tink/jwt/internal/jwt_hmac_key_manager.h"

#include <string>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_split.h"
#include "absl/time/time.h"
#include "tink/core/key_manager_impl.h"
#include "tink/jwt/internal/json_util.h"
#include "tink/jwt/internal/jwt_format.h"
#include "tink/mac.h"
#include "tink/util/istream_input_stream.h"
#include "tink/util/secret_data.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
#include "tink/util/test_matchers.h"
#include "tink/util/test_util.h"

namespace crypto {
namespace tink {
namespace jwt_internal {

using ::crypto::tink::test::IsOk;
using ::crypto::tink::test::StatusIs;
using ::crypto::tink::util::IstreamInputStream;
using ::crypto::tink::util::StatusOr;
using ::google::crypto::tink::JwtHmacAlgorithm;
using ::google::crypto::tink::JwtHmacKey;
using ::google::crypto::tink::JwtHmacKeyFormat;
using ::google::crypto::tink::KeyData;
using ::testing::Eq;
using ::testing::Not;
using ::testing::SizeIs;

namespace {

TEST(JwtHmacKeyManagerTest, Basics) {
  EXPECT_EQ(JwtHmacKeyManager().get_version(), 0);
  EXPECT_EQ(JwtHmacKeyManager().get_key_type(),
            "type.googleapis.com/google.crypto.tink.JwtHmacKey");
  EXPECT_EQ(JwtHmacKeyManager().key_material_type(),
            google::crypto::tink::KeyData::SYMMETRIC);
}

TEST(JwtHmacKeyManagerTest, ValidateEmptyKey) {
  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(JwtHmacKey()), Not(IsOk()));
}

TEST(JwtHmacKeyManagerTest, ValidateEmptyKeyFormat) {
  EXPECT_THAT(JwtHmacKeyManager().ValidateKeyFormat(JwtHmacKeyFormat()),
              Not(IsOk()));
}

TEST(JwtHmacKeyManagerTest, ValidKeyFormatHS256) {
  JwtHmacKeyFormat key_format;
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);
  key_format.set_key_size(32);
  EXPECT_THAT(JwtHmacKeyManager().ValidateKeyFormat(key_format), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeyFormatHS384) {
  JwtHmacKeyFormat key_format;
  key_format.set_algorithm(JwtHmacAlgorithm::HS384);
  key_format.set_key_size(32);
  EXPECT_THAT(JwtHmacKeyManager().ValidateKeyFormat(key_format), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeyFormatHS512) {
  JwtHmacKeyFormat key_format;
  key_format.set_algorithm(JwtHmacAlgorithm::HS512);
  key_format.set_key_size(32);
  EXPECT_THAT(JwtHmacKeyManager().ValidateKeyFormat(key_format), IsOk());
}

TEST(JwtHmacKeyManagerTest, KeyTooShort) {
  JwtHmacKeyFormat key_format;
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);

  key_format.set_key_size(31);
  EXPECT_THAT(JwtHmacKeyManager().ValidateKeyFormat(key_format), Not(IsOk()));
}

TEST(JwtHmacKeyManagerTest, CreateKey) {
  JwtHmacKeyFormat key_format;
  key_format.set_key_size(32);
  key_format.set_algorithm(JwtHmacAlgorithm::HS512);
  auto key_or = JwtHmacKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());
  EXPECT_EQ(key_or.ValueOrDie().version(), 0);
  EXPECT_EQ(key_or.ValueOrDie().algorithm(), key_format.algorithm());
  EXPECT_THAT(key_or.ValueOrDie().key_value(), SizeIs(key_format.key_size()));

  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(key_or.ValueOrDie()), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeyWithUnknownAlgorithm_fails) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS_UNKNOWN);
  key.set_key_value("0123456789abcdef0123456789abcdef");

  EXPECT_FALSE(JwtHmacKeyManager().ValidateKey(key).ok());
}

TEST(JwtHmacKeyManagerTest, ValidateKeySha256) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS256);
  key.set_key_value("0123456789abcdef0123456789abcdef");

  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(key), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeySha384) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS384);
  key.set_key_value("0123456789abcdef0123456789abcdef");

  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(key), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeySha512) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS512);
  key.set_key_value("0123456789abcdef0123456789abcdef");

  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(key), IsOk());
}

TEST(JwtHmacKeyManagerTest, ValidateKeyTooShort) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS256);
  key.set_key_value("0123456789abcdef0123456789abcde");

  EXPECT_THAT(JwtHmacKeyManager().ValidateKey(key), Not(IsOk()));
}

TEST(JwtHmacKeyManagerTest, DeriveKeyIsNotImplemented) {
  JwtHmacKeyFormat format;
  format.set_version(0);
  format.set_key_size(32);
  format.set_algorithm(JwtHmacAlgorithm::HS256);

  IstreamInputStream input_stream{
      absl::make_unique<std::stringstream>("0123456789abcdefghijklmnop")};

  ASSERT_THAT(JwtHmacKeyManager().DeriveKey(format, &input_stream).status(),
              StatusIs(util::error::UNIMPLEMENTED));
}

TEST(JwtHmacKeyManagerTest, GetAndUsePrimitive) {
  JwtHmacKeyFormat key_format;
  key_format.set_key_size(32);
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);
  auto key_or = JwtHmacKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());

  auto jwt_mac_or =
      JwtHmacKeyManager().GetPrimitive<JwtMacInternal>(key_or.ValueOrDie());
  ASSERT_THAT(jwt_mac_or.status(), IsOk());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  auto raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_mac_or.ValueOrDie()->ComputeMacAndEncodeWithKid(raw_jwt,
                                                          absl::nullopt);
  ASSERT_THAT(compact_or.status(), IsOk());
  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectIssuer("issuer")
                               .AllowMissingExpiration()
                               .Build()
                               .ValueOrDie();

  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_mac_or.ValueOrDie()->VerifyMacAndDecode(compact_or.ValueOrDie(),
                                                  validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  util::StatusOr<std::string> issuer_or =
      verified_jwt_or.ValueOrDie().GetIssuer();
  ASSERT_THAT(issuer_or.status(), IsOk());
  EXPECT_THAT(issuer_or.ValueOrDie(), testing::Eq("issuer"));
}

TEST(JwtHmacKeyManagerTest, GetAndUsePrimitiveWithKid) {
  JwtHmacKeyFormat key_format;
  key_format.set_key_size(32);
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);
  auto key_or = JwtHmacKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());

  auto jwt_mac_or =
      JwtHmacKeyManager().GetPrimitive<JwtMacInternal>(key_or.ValueOrDie());
  ASSERT_THAT(jwt_mac_or.status(), IsOk());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  auto raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_mac_or.ValueOrDie()->ComputeMacAndEncodeWithKid(raw_jwt, "kid-123");
  ASSERT_THAT(compact_or.status(), IsOk());
  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectIssuer("issuer")
                               .AllowMissingExpiration()
                               .Build()
                               .ValueOrDie();

  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_mac_or.ValueOrDie()->VerifyMacAndDecode(compact_or.ValueOrDie(),
                                                  validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  util::StatusOr<std::string> issuer_or =
      verified_jwt_or.ValueOrDie().GetIssuer();
  ASSERT_THAT(issuer_or.status(), IsOk());
  EXPECT_THAT(issuer_or.ValueOrDie(), testing::Eq("issuer"));

  // parse header to make sure kid value is set correctly.
  std::vector<absl::string_view> parts =
      absl::StrSplit(compact_or.ValueOrDie(), '.');
  ASSERT_THAT(parts.size(), Eq(3));
  std::string json_header;
  ASSERT_TRUE(DecodeHeader(parts[0], &json_header));
  auto header_or = JsonStringToProtoStruct(json_header);
  ASSERT_THAT(header_or.status(), IsOk());
  EXPECT_THAT(
      header_or.ValueOrDie().fields().find("kid")->second.string_value(),
      Eq("kid-123"));
}

TEST(JwtHmacKeyManagerTest, GetAndUsePrimitiveWithCustomKid) {
  JwtHmacKeyFormat key_format;
  key_format.set_key_size(32);
  key_format.set_algorithm(JwtHmacAlgorithm::HS256);
  util::StatusOr<JwtHmacKey> key = JwtHmacKeyManager().CreateKey(key_format);
  ASSERT_THAT(key.status(), IsOk());
  key->mutable_custom_kid()->set_value(
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit");

  auto jwt_mac = JwtHmacKeyManager().GetPrimitive<JwtMacInternal>(*key);
  ASSERT_THAT(jwt_mac.status(), IsOk());

  auto raw_jwt =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt.status(), IsOk());

  util::StatusOr<std::string> compact =
      (*jwt_mac)->ComputeMacAndEncodeWithKid(*raw_jwt, absl::nullopt);
  ASSERT_THAT(compact.status(), IsOk());
  util::StatusOr<JwtValidator> validator = JwtValidatorBuilder()
                                               .ExpectIssuer("issuer")
                                               .AllowMissingExpiration()
                                               .Build();
  ASSERT_THAT(validator.status(), IsOk());
  // parse header and check "kid"
  std::vector<absl::string_view> parts = absl::StrSplit(*compact, '.');
  ASSERT_THAT(parts.size(), Eq(3));
  std::string json_header;
  ASSERT_TRUE(DecodeHeader(parts[0], &json_header));
  util::StatusOr<google::protobuf::Struct> header =
      JsonStringToProtoStruct(json_header);
  ASSERT_THAT(header.status(), IsOk());
  auto it = header->fields().find("kid");
  ASSERT_FALSE(it == header->fields().end());
  EXPECT_THAT(it->second.string_value(),
              Eq("Lorem ipsum dolor sit amet, consectetur adipiscing elit"));

  // validate token
  util::StatusOr<VerifiedJwt> verified_jwt =
      (*jwt_mac)->VerifyMacAndDecode(*compact, *validator);
  ASSERT_THAT(verified_jwt.status(), IsOk());
  util::StatusOr<std::string> issuer = verified_jwt->GetIssuer();
  ASSERT_THAT(issuer.status(), IsOk());
  EXPECT_THAT(*issuer, testing::Eq("issuer"));

  // passing a kid when custom_kid is set should fail
  util::StatusOr<std::string> compact2 =
      (*jwt_mac)->ComputeMacAndEncodeWithKid(*raw_jwt, "kid123");
  ASSERT_FALSE(compact2.ok());
}

TEST(JwtHmacKeyManagerTest, ValidateTokenWithFixedKey) {
  JwtHmacKey key;
  key.set_version(0);
  key.set_algorithm(JwtHmacAlgorithm::HS256);

  std::string key_value;
  ASSERT_TRUE(absl::WebSafeBase64Unescape(
      "AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1"
      "qS0gZH75aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow",
      &key_value));
  key.set_key_value(key_value);
  auto jwt_mac_or = JwtHmacKeyManager().GetPrimitive<JwtMacInternal>(key);
  ASSERT_THAT(jwt_mac_or.status(), IsOk());

  std::string compact =
      "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJqb2UiLA0KICJleH"
      "AiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ."
      "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectTypeHeader("JWT")
                               .ExpectIssuer("joe")
                               .SetFixedNow(absl::FromUnixSeconds(12345))
                               .Build()
                               .ValueOrDie();

  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_mac_or.ValueOrDie()->VerifyMacAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  auto verified_jwt = verified_jwt_or.ValueOrDie();
  EXPECT_THAT(verified_jwt.GetIssuer(), test::IsOkAndHolds("joe"));
  EXPECT_THAT(verified_jwt.GetBooleanClaim("http://example.com/is_root"),
              test::IsOkAndHolds(true));

  JwtValidator validator_now = JwtValidatorBuilder().Build().ValueOrDie();
  EXPECT_FALSE(
      jwt_mac_or.ValueOrDie()->VerifyMacAndDecode(compact, validator_now).ok());

  std::string modified_compact =
      "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJqb2UiLA0KICJleH"
      "AiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ."
      "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXi";
  EXPECT_FALSE(jwt_mac_or.ValueOrDie()
                   ->VerifyMacAndDecode(modified_compact, validator)
                   .ok());
}

}  // namespace
}  // namespace jwt_internal
}  // namespace tink
}  // namespace crypto
