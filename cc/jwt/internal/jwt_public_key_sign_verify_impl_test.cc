// Copyright 2021 Google LLC.
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
///////////////////////////////////////////////////////////////////////////////

#include <string>
#include <utility>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "tink/jwt/internal/json_util.h"
#include "tink/jwt/internal/jwt_format.h"
#include "tink/jwt/internal/jwt_public_key_sign_impl.h"
#include "tink/jwt/internal/jwt_public_key_verify_impl.h"
#include "tink/jwt/jwt_public_key_sign.h"
#include "tink/jwt/jwt_public_key_verify.h"
#include "tink/jwt/jwt_validator.h"
#include "tink/jwt/raw_jwt.h"
#include "tink/jwt/verified_jwt.h"
#include "tink/subtle/ecdsa_sign_boringssl.h"
#include "tink/subtle/ecdsa_verify_boringssl.h"
#include "tink/util/test_matchers.h"

using ::crypto::tink::test::IsOk;
using ::crypto::tink::test::IsOkAndHolds;
using ::testing::Eq;

namespace crypto {
namespace tink {
namespace jwt_internal {

namespace {

class JwtSignatureImplTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto ec_key_result = subtle::SubtleUtilBoringSSL::GetNewEcKey(
        subtle::EllipticCurveType::NIST_P256);
    ASSERT_THAT(ec_key_result.status(), IsOk());
    auto ec_key = ec_key_result.ValueOrDie();

    auto sign_result = subtle::EcdsaSignBoringSsl::New(
        ec_key, subtle::HashType::SHA256,
        subtle::EcdsaSignatureEncoding::IEEE_P1363);
    ASSERT_THAT(sign_result.status(), IsOk());

    auto verify_result = subtle::EcdsaVerifyBoringSsl::New(
        ec_key, subtle::HashType::SHA256,
        subtle::EcdsaSignatureEncoding::IEEE_P1363);
    ASSERT_THAT(verify_result.status(), IsOk());

    jwt_sign_ = absl::make_unique<JwtPublicKeySignImpl>(
        std::move(sign_result.ValueOrDie()), "ES256", absl::nullopt);
    jwt_verify_ = absl::make_unique<JwtPublicKeyVerifyImpl>(
        std::move(verify_result.ValueOrDie()), "ES256");
  }
  std::unique_ptr<JwtPublicKeySignImpl> jwt_sign_;
  std::unique_ptr<JwtPublicKeyVerifyImpl> jwt_verify_;
};

TEST_F(JwtSignatureImplTest, CreateAndValidateToken) {
  absl::Time now = absl::Now();
  util::StatusOr<RawJwt> raw_jwt_or =
      RawJwtBuilder()
          .SetTypeHeader("typeHeader")
          .SetJwtId("id123")
          .SetNotBefore(now - absl::Seconds(300))
          .SetIssuedAt(now)
          .SetExpiration(now + absl::Seconds(300))
          .Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  RawJwt raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_sign_->SignAndEncodeWithKid(raw_jwt, absl::nullopt);
  ASSERT_THAT(compact_or.status(), IsOk());
  std::string compact = compact_or.ValueOrDie();

  JwtValidator validator =
      JwtValidatorBuilder().ExpectTypeHeader("typeHeader").Build().ValueOrDie();

  // Success
  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_verify_->VerifyAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  auto verified_jwt = verified_jwt_or.ValueOrDie();
  EXPECT_THAT(verified_jwt.GetTypeHeader(), IsOkAndHolds("typeHeader"));
  EXPECT_THAT(verified_jwt.GetJwtId(), IsOkAndHolds("id123"));

  // Fails with wrong issuer
  JwtValidator validator2 =
      JwtValidatorBuilder().ExpectIssuer("unknown").Build().ValueOrDie();
  EXPECT_FALSE(jwt_verify_->VerifyAndDecode(compact, validator2).ok());

  // Fails because token is not yet valid
  JwtValidator validator_1970 = JwtValidatorBuilder()
                                    .SetFixedNow(absl::FromUnixSeconds(12345))
                                    .Build()
                                    .ValueOrDie();
  EXPECT_FALSE(jwt_verify_->VerifyAndDecode(compact, validator_1970).ok());
}

TEST_F(JwtSignatureImplTest, CreateAndValidateTokenWithKid) {
  absl::Time now = absl::Now();
  util::StatusOr<RawJwt> raw_jwt_or =
      RawJwtBuilder()
          .SetTypeHeader("typeHeader")
          .SetJwtId("id123")
          .SetNotBefore(now - absl::Seconds(300))
          .SetIssuedAt(now)
          .SetExpiration(now + absl::Seconds(300))
          .Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  RawJwt raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_sign_->SignAndEncodeWithKid(raw_jwt, "kid-123");
  ASSERT_THAT(compact_or.status(), IsOk());
  std::string compact = compact_or.ValueOrDie();

  JwtValidator validator =
      JwtValidatorBuilder().ExpectTypeHeader("typeHeader").Build().ValueOrDie();

  util::StatusOr<VerifiedJwt> verified_jwt_or =
      jwt_verify_->VerifyAndDecode(compact, validator);
  ASSERT_THAT(verified_jwt_or.status(), IsOk());
  auto verified_jwt = verified_jwt_or.ValueOrDie();
  EXPECT_THAT(verified_jwt.GetTypeHeader(), IsOkAndHolds("typeHeader"));
  EXPECT_THAT(verified_jwt.GetJwtId(), IsOkAndHolds("id123"));

  // parse header to make sure the kid value is set correctly.
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

TEST_F(JwtSignatureImplTest, FailsWithModifiedCompact) {
  auto raw_jwt_or =
      RawJwtBuilder().SetJwtId("id123").WithoutExpiration().Build();
  ASSERT_THAT(raw_jwt_or.status(), IsOk());
  RawJwt raw_jwt = raw_jwt_or.ValueOrDie();

  util::StatusOr<std::string> compact_or =
      jwt_sign_->SignAndEncodeWithKid(raw_jwt, absl::nullopt);
  ASSERT_THAT(compact_or.status(), IsOk());
  std::string compact = compact_or.ValueOrDie();
  JwtValidator validator =
      JwtValidatorBuilder().AllowMissingExpiration().Build().ValueOrDie();

  EXPECT_THAT(jwt_verify_->VerifyAndDecode(compact, validator).status(),
              IsOk());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode(absl::StrCat(compact, "x"), validator).ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode(absl::StrCat(compact, " "), validator).ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode(absl::StrCat("x", compact), validator).ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode(absl::StrCat(" ", compact), validator).ok());
}

TEST_F(JwtSignatureImplTest, FailsWithInvalidTokens) {
  JwtValidator validator =
      JwtValidatorBuilder().AllowMissingExpiration().Build().ValueOrDie();
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode("eyJhbGciOiJIUzI1NiJ9.e30.YWJj.", validator)
          .ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode("eyJhbGciOiJIUzI1NiJ9?.e30.YWJj", validator)
          .ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode("eyJhbGciOiJIUzI1NiJ9.e30?.YWJj", validator)
          .ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode("eyJhbGciOiJIUzI1NiJ9.e30.YWJj?", validator)
          .ok());
  EXPECT_FALSE(
      jwt_verify_->VerifyAndDecode("eyJhbGciOiJIUzI1NiJ9.YWJj", validator)
          .ok());
  EXPECT_FALSE(jwt_verify_->VerifyAndDecode("", validator).ok());
  EXPECT_FALSE(jwt_verify_->VerifyAndDecode("..", validator).ok());
}

}  // namespace
}  // namespace jwt_internal
}  // namespace tink
}  // namespace crypto
