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
///////////////////////////////////////////////////////////////////////////////

#include "jwt_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "tink/binary_keyset_writer.h"
#include "tink/cleartext_keyset_handle.h"
#include "tink/jwt/jwt_mac_config.h"
#include "tink/jwt/jwt_signature_config.h"
#include "tink/jwt/jwt_key_templates.h"
#include "proto/testing/testing_api.grpc.pb.h"
#include "tink/util/test_matchers.h"

namespace crypto {
namespace tink {
namespace {

using ::crypto::tink::BinaryKeysetWriter;
using ::crypto::tink::CleartextKeysetHandle;
using ::crypto::tink::KeysetHandle;
using ::google::crypto::tink::KeyTemplate;
using ::testing::Eq;
using ::testing::IsEmpty;
using ::crypto::tink::test::IsOk;
using ::tink_testing_api::JwtSignRequest;
using ::tink_testing_api::JwtSignResponse;
using ::tink_testing_api::JwtVerifyRequest;
using ::tink_testing_api::JwtVerifyResponse;

std::string ValidKeyset() {
  const KeyTemplate& key_template = ::crypto::tink::JwtHs256Template();
  auto handle_or = KeysetHandle::GenerateNew(key_template);
  EXPECT_THAT(handle_or.status(), IsOk());

  std::stringbuf keyset;
  auto writer_or =
      BinaryKeysetWriter::New(absl::make_unique<std::ostream>(&keyset));
  EXPECT_THAT(writer_or.status(), IsOk());

  auto status = CleartextKeysetHandle::Write(writer_or.ValueOrDie().get(),
                                             *handle_or.ValueOrDie());
  EXPECT_THAT(status, IsOk());
  return keyset.str();
}

class JwtImplMacTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_THAT(JwtMacRegister(), IsOk()); }
};

TEST_F(JwtImplMacTest, MacComputeVerifySuccess) {
  tink_testing_api::JwtImpl jwt;
  std::string keyset = ValidKeyset();
  JwtSignRequest comp_request;
  comp_request.set_keyset(keyset);
  auto raw_jwt = comp_request.mutable_raw_jwt();
  raw_jwt->mutable_type_header()->set_value("type_header");
  raw_jwt->mutable_issuer()->set_value("issuer");
  raw_jwt->mutable_subject()->set_value("subject");
  raw_jwt->mutable_audiences()->Add("audience1");
  raw_jwt->mutable_audiences()->Add("audience2");
  raw_jwt->mutable_jwt_id()->set_value("jwt_id");
  raw_jwt->mutable_not_before()->set_seconds(12345);
  raw_jwt->mutable_not_before()->set_nanos(123000000);
  raw_jwt->mutable_issued_at()->set_seconds(23456);
  raw_jwt->mutable_expiration()->set_seconds(34567);
  auto custom_claims = raw_jwt->mutable_custom_claims();
  (*custom_claims)["null_claim"].set_null_value(
      tink_testing_api::NullValue::NULL_VALUE);
  (*custom_claims)["bool_claim"].set_bool_value(true);
  (*custom_claims)["number_claim"].set_number_value(123.456);
  (*custom_claims)["string_claim"].set_string_value("string_value");
  JwtSignResponse comp_response;
  EXPECT_TRUE(
      jwt.ComputeMacAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), IsEmpty());

  JwtVerifyRequest verify_request;
  verify_request.set_keyset(keyset);
  verify_request.set_signed_compact_jwt(comp_response.signed_compact_jwt());
  auto validator = verify_request.mutable_validator();
  validator->mutable_expected_type_header()->set_value("type_header");
  validator->mutable_expected_issuer()->set_value("issuer");
  validator->mutable_expected_subject()->set_value("subject");
  validator->mutable_expected_audience()->set_value("audience2");
  validator->mutable_now()->set_seconds(23456);
  JwtVerifyResponse verify_response;

  ASSERT_TRUE(
      jwt.VerifyMacAndDecode(nullptr, &verify_request, &verify_response).ok());
  ASSERT_THAT(verify_response.err(), IsEmpty());
  auto verified_jwt = verify_response.verified_jwt();
  EXPECT_THAT(verified_jwt.type_header().value(), Eq("type_header"));
  EXPECT_THAT(verified_jwt.issuer().value(), Eq("issuer"));
  EXPECT_THAT(verified_jwt.subject().value(), Eq("subject"));
  EXPECT_THAT(verified_jwt.audiences_size(), Eq(2));
  EXPECT_THAT(verified_jwt.audiences(0), Eq("audience1"));
  EXPECT_THAT(verified_jwt.audiences(1), Eq("audience2"));
  EXPECT_THAT(verified_jwt.jwt_id().value(), Eq("jwt_id"));
  EXPECT_THAT(verified_jwt.not_before().seconds(), Eq(12345));
  EXPECT_THAT(verified_jwt.not_before().nanos(), Eq(0));
  EXPECT_THAT(verified_jwt.issued_at().seconds(), Eq(23456));
  EXPECT_THAT(verified_jwt.issued_at().nanos(), Eq(0));
  EXPECT_THAT(verified_jwt.expiration().seconds(), Eq(34567));
  EXPECT_THAT(verified_jwt.expiration().nanos(), Eq(0));
  auto verified_custom_claims = verified_jwt.custom_claims();
  EXPECT_THAT(verified_custom_claims["null_claim"].null_value(),
              Eq(tink_testing_api::NullValue::NULL_VALUE));
  EXPECT_THAT(verified_custom_claims["bool_claim"].bool_value(), Eq(true));
  EXPECT_THAT(verified_custom_claims["number_claim"].number_value(),
              Eq(123.456));
  EXPECT_THAT(verified_custom_claims["string_claim"].string_value(),
              Eq("string_value"));
}

TEST_F(JwtImplMacTest, ComputeBadKeysetFail) {
  tink_testing_api::JwtImpl jwt;
  JwtSignRequest comp_request;
  comp_request.set_keyset("bad keyset");
  comp_request.mutable_raw_jwt()->mutable_issuer()->set_value("issuer");
  JwtSignResponse comp_response;

  EXPECT_TRUE(
      jwt.ComputeMacAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), Not(IsEmpty()));
}

TEST_F(JwtImplMacTest, VerifyWithWrongIssuerFails) {
  tink_testing_api::JwtImpl jwt;
  std::string keyset = ValidKeyset();
  JwtSignRequest comp_request;
  comp_request.set_keyset(keyset);
  comp_request.mutable_raw_jwt()->mutable_issuer()->set_value("unknown");
  JwtSignResponse comp_response;
  EXPECT_TRUE(
      jwt.ComputeMacAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), IsEmpty());

  JwtVerifyRequest verify_request;
  verify_request.set_keyset(keyset);
  verify_request.set_signed_compact_jwt(comp_response.signed_compact_jwt());
  verify_request.mutable_validator()->mutable_expected_issuer()->set_value(
      "issuer");
  JwtVerifyResponse verify_response;

  EXPECT_TRUE(
      jwt.VerifyMacAndDecode(nullptr, &verify_request, &verify_response).ok());
  EXPECT_THAT(verify_response.err(), Not(IsEmpty()));
}

class JwtImplSignatureTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() { ASSERT_THAT(JwtSignatureRegister(), IsOk()); }
  void SetUp() override {
    const KeyTemplate& key_template = ::crypto::tink::JwtEs256Template();
    auto handle_or = KeysetHandle::GenerateNew(key_template);
    EXPECT_THAT(handle_or.status(), IsOk());

    std::stringbuf keyset;
    auto writer_or =
        BinaryKeysetWriter::New(absl::make_unique<std::ostream>(&keyset));
    EXPECT_THAT(writer_or.status(), IsOk());

    auto status = CleartextKeysetHandle::Write(writer_or.ValueOrDie().get(),
                                               *handle_or.ValueOrDie());
    EXPECT_THAT(status, IsOk());
    private_keyset_ = keyset.str();

    auto pub_handle_or = handle_or.ValueOrDie()->GetPublicKeysetHandle();
    EXPECT_THAT(pub_handle_or.status(), IsOk());

    std::stringbuf pub_keyset;
    auto pub_writer_or = BinaryKeysetWriter::New(
        absl::make_unique<std::ostream>(&pub_keyset));
    EXPECT_THAT(writer_or.status(), IsOk());

    auto pub_status = CleartextKeysetHandle::Write(
        pub_writer_or.ValueOrDie().get(), *pub_handle_or.ValueOrDie());
    EXPECT_THAT(pub_status, IsOk());
    public_keyset_ = pub_keyset.str();
  }
  std::string private_keyset_;
  std::string public_keyset_;
};

TEST_F(JwtImplSignatureTest, SignVerifySuccess) {
  tink_testing_api::JwtImpl jwt;
  JwtSignRequest comp_request;
  comp_request.set_keyset(private_keyset_);
  auto raw_jwt = comp_request.mutable_raw_jwt();
  raw_jwt->mutable_type_header()->set_value("type_header");
  raw_jwt->mutable_issuer()->set_value("issuer");
  raw_jwt->mutable_subject()->set_value("subject");
  raw_jwt->mutable_audiences()->Add("audience1");
  raw_jwt->mutable_audiences()->Add("audience2");
  raw_jwt->mutable_jwt_id()->set_value("jwt_id");
  raw_jwt->mutable_not_before()->set_seconds(12345);
  raw_jwt->mutable_issued_at()->set_seconds(23456);
  raw_jwt->mutable_expiration()->set_seconds(34567);
  auto custom_claims = raw_jwt->mutable_custom_claims();
  (*custom_claims)["null_claim"].set_null_value(
      tink_testing_api::NullValue::NULL_VALUE);
  (*custom_claims)["bool_claim"].set_bool_value(true);
  (*custom_claims)["number_claim"].set_number_value(123.456);
  (*custom_claims)["string_claim"].set_string_value("string_value");
  JwtSignResponse comp_response;
  EXPECT_TRUE(
      jwt.PublicKeySignAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), IsEmpty());

  JwtVerifyRequest verify_request;
  verify_request.set_keyset(public_keyset_);
  verify_request.set_signed_compact_jwt(comp_response.signed_compact_jwt());
  auto validator = verify_request.mutable_validator();
  validator->mutable_expected_type_header()->set_value("type_header");
  validator->mutable_expected_issuer()->set_value("issuer");
  validator->mutable_expected_subject()->set_value("subject");
  validator->mutable_expected_audience()->set_value("audience2");
  validator->mutable_now()->set_seconds(23456);
  JwtVerifyResponse verify_response;

  ASSERT_TRUE(
      jwt.PublicKeyVerifyAndDecode(nullptr, &verify_request, &verify_response)
          .ok());
  ASSERT_THAT(verify_response.err(), IsEmpty());
  auto verified_jwt = verify_response.verified_jwt();
  EXPECT_THAT(verified_jwt.type_header().value(), Eq("type_header"));
  EXPECT_THAT(verified_jwt.issuer().value(), Eq("issuer"));
  EXPECT_THAT(verified_jwt.subject().value(), Eq("subject"));
  EXPECT_THAT(verified_jwt.audiences_size(), Eq(2));
  EXPECT_THAT(verified_jwt.audiences(0), Eq("audience1"));
  EXPECT_THAT(verified_jwt.audiences(1), Eq("audience2"));
  EXPECT_THAT(verified_jwt.jwt_id().value(), Eq("jwt_id"));
  EXPECT_THAT(verified_jwt.not_before().seconds(), Eq(12345));
  EXPECT_THAT(verified_jwt.issued_at().seconds(), Eq(23456));
  EXPECT_THAT(verified_jwt.expiration().seconds(), Eq(34567));
  auto verified_custom_claims = verified_jwt.custom_claims();
  EXPECT_THAT(verified_custom_claims["null_claim"].null_value(),
              Eq(tink_testing_api::NullValue::NULL_VALUE));
  EXPECT_THAT(verified_custom_claims["bool_claim"].bool_value(), Eq(true));
  EXPECT_THAT(verified_custom_claims["number_claim"].number_value(),
              Eq(123.456));
  EXPECT_THAT(verified_custom_claims["string_claim"].string_value(),
              Eq("string_value"));
}

TEST_F(JwtImplSignatureTest, SignWithBadKeysetFails) {
  tink_testing_api::JwtImpl jwt;
  JwtSignRequest comp_request;
  comp_request.set_keyset("bad keyset");
  comp_request.mutable_raw_jwt()->mutable_issuer()->set_value("issuer");
  JwtSignResponse comp_response;

  EXPECT_TRUE(
      jwt.PublicKeySignAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), Not(IsEmpty()));
}

TEST_F(JwtImplSignatureTest, VerifyWithWrongIssuerFails) {
  tink_testing_api::JwtImpl jwt;
  JwtSignRequest comp_request;
  comp_request.set_keyset(private_keyset_);
  comp_request.mutable_raw_jwt()->mutable_issuer()->set_value("unknown");
  JwtSignResponse comp_response;
  EXPECT_TRUE(
      jwt.PublicKeySignAndEncode(nullptr, &comp_request, &comp_response).ok());
  EXPECT_THAT(comp_response.err(), IsEmpty());

  JwtVerifyRequest verify_request;
  verify_request.set_keyset(public_keyset_);
  verify_request.set_signed_compact_jwt(comp_response.signed_compact_jwt());
  verify_request.mutable_validator()->mutable_expected_issuer()->set_value(
      "issuer");
  JwtVerifyResponse verify_response;

  EXPECT_TRUE(
      jwt.PublicKeyVerifyAndDecode(nullptr, &verify_request, &verify_response)
          .ok());
  EXPECT_THAT(verify_response.err(), Not(IsEmpty()));
}

}  // namespace
}  // namespace tink
}  // namespace crypto

