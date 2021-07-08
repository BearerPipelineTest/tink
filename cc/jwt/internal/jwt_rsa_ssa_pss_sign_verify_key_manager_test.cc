// Copyright 2021 Google LLC.
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

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_split.h"
#include "openssl/bn.h"
#include "openssl/rsa.h"
#include "tink/jwt/internal/json_util.h"
#include "tink/jwt/internal/jwt_format.h"
#include "tink/jwt/internal/jwt_rsa_ssa_pss_sign_key_manager.h"
#include "tink/jwt/internal/jwt_rsa_ssa_pss_verify_key_manager.h"
#include "tink/subtle/subtle_util_boringssl.h"
#include "tink/util/status.h"
#include "tink/util/statusor.h"
#include "tink/util/test_matchers.h"

namespace crypto {
namespace tink {
namespace jwt_internal {

using ::crypto::tink::test::IsOk;
using ::crypto::tink::util::StatusOr;
using ::google::crypto::tink::JwtRsaSsaPssAlgorithm;
using ::google::crypto::tink::JwtRsaSsaPssKeyFormat;
using ::google::crypto::tink::JwtRsaSsaPssPrivateKey;
using ::google::crypto::tink::JwtRsaSsaPssPublicKey;
using ::google::crypto::tink::KeyData;
using ::testing::Eq;
using ::testing::Not;

namespace {

JwtRsaSsaPssKeyFormat CreateKeyFormat(JwtRsaSsaPssAlgorithm algorithm,
                                      int modulus_size_in_bits,
                                      int public_exponent) {
  JwtRsaSsaPssKeyFormat key_format;
  key_format.set_algorithm(algorithm);
  key_format.set_modulus_size_in_bits(modulus_size_in_bits);
  bssl::UniquePtr<BIGNUM> e(BN_new());
  BN_set_word(e.get(), public_exponent);
  key_format.set_public_exponent(
      subtle::SubtleUtilBoringSSL::bn2str(e.get(), BN_num_bytes(e.get()))
          .ValueOrDie());
  return key_format;
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, BasicsSign) {
  EXPECT_EQ(JwtRsaSsaPssSignKeyManager().get_version(), 0);
  EXPECT_EQ(JwtRsaSsaPssSignKeyManager().get_key_type(),
            "type.googleapis.com/google.crypto.tink.JwtRsaSsaPssPrivateKey");
  EXPECT_EQ(JwtRsaSsaPssSignKeyManager().key_material_type(),
            google::crypto::tink::KeyData::ASYMMETRIC_PRIVATE);
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, BasicsVerify) {
  EXPECT_EQ(JwtRsaSsaPssVerifyKeyManager().get_version(), 0);
  EXPECT_EQ(JwtRsaSsaPssVerifyKeyManager().get_key_type(),
            "type.googleapis.com/google.crypto.tink.JwtRsaSsaPssPublicKey");
  EXPECT_EQ(JwtRsaSsaPssVerifyKeyManager().key_material_type(),
            google::crypto::tink::KeyData::ASYMMETRIC_PUBLIC);
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidateEmptyPrivateKey) {
  EXPECT_THAT(
      JwtRsaSsaPssSignKeyManager().ValidateKey(JwtRsaSsaPssPrivateKey()),
      Not(IsOk()));
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidateEmptyPublicKey) {
  EXPECT_THAT(
      JwtRsaSsaPssVerifyKeyManager().ValidateKey(JwtRsaSsaPssPublicKey()),
      Not(IsOk()));
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidateEmptyKeyFormat) {
  EXPECT_THAT(
      JwtRsaSsaPssSignKeyManager().ValidateKeyFormat(JwtRsaSsaPssKeyFormat()),
      Not(IsOk()));
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidKeyFormatPS256) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  EXPECT_THAT(JwtRsaSsaPssSignKeyManager().ValidateKeyFormat(key_format),
              IsOk());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidateKeyFormatRS384) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS384, 3072, RSA_F4);
  EXPECT_THAT(JwtRsaSsaPssSignKeyManager().ValidateKeyFormat(key_format),
              IsOk());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, ValidateKeyFormatRS512) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS512, 4096, RSA_F4);
  EXPECT_THAT(JwtRsaSsaPssSignKeyManager().ValidateKeyFormat(key_format),
              IsOk());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, CreatePrivateKeyAndValidate) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  auto key_or = JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());
  auto key = key_or.ValueOrDie();
  EXPECT_EQ(key.version(), 0);
  EXPECT_EQ(key.public_key().algorithm(), key_format.algorithm());
  EXPECT_THAT(JwtRsaSsaPssSignKeyManager().ValidateKey(key), IsOk());

  // Change key to an invalid algorithm.
  key.mutable_public_key()->set_algorithm(JwtRsaSsaPssAlgorithm::PS_UNKNOWN);
  EXPECT_FALSE(JwtRsaSsaPssSignKeyManager().ValidateKey(key).ok());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, CreatePublicKeyAndValidate) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  auto key_or = JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());
  auto public_key_or =
      JwtRsaSsaPssSignKeyManager().GetPublicKey(key_or.ValueOrDie());
  auto public_key = public_key_or.ValueOrDie();
  EXPECT_THAT(JwtRsaSsaPssVerifyKeyManager().ValidateKey(public_key), IsOk());

  // Change key to an invalid algorithm.
  public_key.set_algorithm(JwtRsaSsaPssAlgorithm::PS_UNKNOWN);
  EXPECT_FALSE(JwtRsaSsaPssVerifyKeyManager().ValidateKey(public_key).ok());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, GetAndUsePrimitives) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  auto key_or = JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key_or.status(), IsOk());
  auto key = key_or.ValueOrDie();

  auto sign_or =
      JwtRsaSsaPssSignKeyManager().GetPrimitive<JwtPublicKeySignInternal>(key);
  ASSERT_THAT(sign_or.status(), IsOk());
  auto sign = std::move(sign_or.ValueOrDie());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  auto raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      sign->SignAndEncodeWithKid(raw_jwt, absl::nullopt);
  ASSERT_THAT(compact_or.status(), IsOk());
  auto compact = compact_or.ValueOrDie();

  JwtValidator validator = JwtValidatorBuilder()
                               .ExpectIssuer("issuer")
                               .AllowMissingExpiration()
                               .Build()
                               .ValueOrDie();
  auto verify_or =
      JwtRsaSsaPssVerifyKeyManager().GetPrimitive<JwtPublicKeyVerify>(
          key.public_key());
  ASSERT_THAT(verify_or.status(), IsOk());
  auto verify = std::move(verify_or.ValueOrDie());

  util::StatusOr<VerifiedJwt> verified_jwt_or =
      verify->VerifyAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  util::StatusOr<std::string> issuer_or =
      verified_jwt_or.ValueOrDie().GetIssuer();
  ASSERT_THAT(issuer_or.status(), IsOk());
  EXPECT_THAT(issuer_or.ValueOrDie(), testing::Eq("issuer"));

  JwtValidator validator2 = JwtValidatorBuilder()
                                .ExpectIssuer("unknown")
                                .AllowMissingExpiration()
                                .Build()
                                .ValueOrDie();
  EXPECT_FALSE(verify->VerifyAndDecode(compact, validator2).ok());
}

TEST(JwtRsaSsaPkcs1SignVerifyKeyManagerTest, GetAndUsePrimitivesWithCustomKid) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  util::StatusOr<JwtRsaSsaPssPrivateKey> key =
      JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key.status(), IsOk());
  key->mutable_public_key()->mutable_custom_kid()->set_value(
      "Lorem ipsum dolor sit amet, consectetur adipiscing elit");

  util::StatusOr<std::unique_ptr<JwtPublicKeySignInternal>> sign =
      JwtRsaSsaPssSignKeyManager().GetPrimitive<JwtPublicKeySignInternal>(*key);
  ASSERT_THAT(sign.status(), IsOk());

  util::StatusOr<RawJwt> raw_jwt =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt.status(), IsOk());

  util::StatusOr<std::string> compact =
      (*sign)->SignAndEncodeWithKid(*raw_jwt, absl::nullopt);
  ASSERT_THAT(compact.status(), IsOk());

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
  util::StatusOr<JwtValidator> validator = JwtValidatorBuilder()
                                               .ExpectIssuer("issuer")
                                               .AllowMissingExpiration()
                                               .Build();
  ASSERT_THAT(validator.status(), IsOk());
  util::StatusOr<std::unique_ptr<JwtPublicKeyVerify>> verify =
      JwtRsaSsaPssVerifyKeyManager().GetPrimitive<JwtPublicKeyVerify>(
          key->public_key());
  ASSERT_THAT(verify.status(), IsOk());

  util::StatusOr<VerifiedJwt> verified_jwt =
      (*verify)->VerifyAndDecode(*compact, *validator);
  ASSERT_THAT(verified_jwt.status(), IsOk());
  util::StatusOr<std::string> issuer = verified_jwt->GetIssuer();
  ASSERT_THAT(issuer.status(), IsOk());
  EXPECT_THAT(*issuer, Eq("issuer"));

  // passing a kid when custom_kid is set should fail
  util::StatusOr<std::string> compact2 =
      (*sign)->SignAndEncodeWithKid(*raw_jwt, "kid123");
  ASSERT_FALSE(compact2.ok());
}

TEST(JwtRsaSsaPssSignVerifyKeyManagerTest, VerifyFailsWithDifferentKey) {
  JwtRsaSsaPssKeyFormat key_format =
      CreateKeyFormat(JwtRsaSsaPssAlgorithm::PS256, 2048, RSA_F4);
  auto key1_or = JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key1_or.status(), IsOk());
  auto key1 = key1_or.ValueOrDie();

  auto key2_or = JwtRsaSsaPssSignKeyManager().CreateKey(key_format);
  ASSERT_THAT(key2_or.status(), IsOk());
  auto key2 = key2_or.ValueOrDie();

  auto sign1_or =
      JwtRsaSsaPssSignKeyManager().GetPrimitive<JwtPublicKeySignInternal>(key1);
  ASSERT_THAT(sign1_or.status(), IsOk());
  auto sign1 = std::move(sign1_or.ValueOrDie());

  auto raw_jwt_or =
      RawJwtBuilder().SetIssuer("issuer").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  auto raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      sign1->SignAndEncodeWithKid(raw_jwt, absl::nullopt);
  ASSERT_THAT(compact_or.status(), IsOk());
  auto compact = compact_or.ValueOrDie();

  JwtValidator validator =
      JwtValidatorBuilder().AllowMissingExpiration().Build().ValueOrDie();
  auto verify2_or =
      JwtRsaSsaPssVerifyKeyManager().GetPrimitive<JwtPublicKeyVerify>(
          key2.public_key());
  ASSERT_THAT(verify2_or.status(), IsOk());
  auto verify2 = std::move(verify2_or.ValueOrDie());

  EXPECT_FALSE(verify2->VerifyAndDecode(compact, validator).ok());
}

}  // namespace
}  // namespace jwt_internal
}  // namespace tink
}  // namespace crypto
