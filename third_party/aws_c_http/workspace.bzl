# Copyright 2024 The TensorStore Authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# buildifier: disable=module-docstring

load("@bazel_tools//tools/build_defs/repo:utils.bzl", "maybe")
load("//third_party:repo.bzl", "third_party_http_archive")

def repo():
    maybe(
        third_party_http_archive,
        name = "aws_c_http",
        sha256 = "4c3a4a6845653c1b01162968f936c31aea6b25d3a0bdbf050b2342b8b94fcca6",
        strip_prefix = "aws-c-http-0.8.8",
        urls = [
            "https://storage.googleapis.com/tensorstore-bazel-mirror/github.com/awslabs/aws-c-http/archive/v0.8.8.tar.gz",
        ],
        build_file = Label("//third_party:aws_c_http/aws_c_http.BUILD.bazel"),
        cmake_name = "aws_c_http",
        cmake_target_mapping = {
            "@aws_c_http//:aws_c_http": "aws_c_http::aws_c_http",
        },
        bazel_to_cmake = {},
    )
