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
        name = "aws_c_s3",
        sha256 = "b671006ae2b5c1302e49ca022e0f9e6504cfe171d9e47c3e59c52b2ab8e80ef5",
        strip_prefix = "aws-c-s3-0.6.5",
        urls = [
            "https://storage.googleapis.com/tensorstore-bazel-mirror/github.com/awslabs/aws-c-s3/archive/v0.6.5.tar.gz",
        ],
        build_file = Label("//third_party:aws_c_s3/aws_c_s3.BUILD.bazel"),
        cmake_name = "aws_c_s3",
        cmake_target_mapping = {
            "@aws_c_s3//:aws_c_s3": "aws_c_s3::aws_c_s3",
        },
        bazel_to_cmake = {},
    )
