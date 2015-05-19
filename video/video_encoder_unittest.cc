/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "webrtc/video_encoder.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "webrtc/modules/video_coding/codecs/interface/video_error_codes.h"

namespace webrtc {

const size_t kMaxPayloadSize = 800;

class VideoEncoderSoftwareFallbackWrapperTest : public ::testing::Test {
 protected:
  VideoEncoderSoftwareFallbackWrapperTest()
      : fallback_wrapper_(kVideoCodecVP8, &fake_encoder_) {}

  class CountingFakeEncoder : public VideoEncoder {
   public:
    int32_t InitEncode(const VideoCodec* codec_settings,
                       int32_t number_of_cores,
                       size_t max_payload_size) override {
      ++init_encode_count_;
      return init_encode_return_code_;
    }
    int32_t Encode(const I420VideoFrame& frame,
                   const CodecSpecificInfo* codec_specific_info,
                   const std::vector<VideoFrameType>* frame_types) override {
      ++encode_count_;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t RegisterEncodeCompleteCallback(
        EncodedImageCallback* callback) override {
      encode_complete_callback_ = callback;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t Release() override {
      ++release_count_;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t SetChannelParameters(uint32_t packet_loss, int64_t rtt) override {
      ++set_channel_parameters_count_;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    int32_t SetRates(uint32_t bitrate, uint32_t framerate) override {
      ++set_rates_count_;
      return WEBRTC_VIDEO_CODEC_OK;
    }

    void OnDroppedFrame() override { ++on_dropped_frame_count_; }

    int init_encode_count_ = 0;
    int32_t init_encode_return_code_ = WEBRTC_VIDEO_CODEC_OK;
    int encode_count_ = 0;
    EncodedImageCallback* encode_complete_callback_ = nullptr;
    int release_count_ = 0;
    int set_channel_parameters_count_ = 0;
    int set_rates_count_ = 0;
    int on_dropped_frame_count_ = 0;
  };

  class FakeEncodedImageCallback : public EncodedImageCallback {
   public:
    int32_t Encoded(const EncodedImage& encoded_image,
                    const CodecSpecificInfo* codec_specific_info,
                    const RTPFragmentationHeader* fragmentation) override {
      return ++callback_count_;
    }
    int callback_count_ = 0;
  };

  void UtilizeFallbackEncoder();

  FakeEncodedImageCallback callback_;
  CountingFakeEncoder fake_encoder_;
  VideoEncoderSoftwareFallbackWrapper fallback_wrapper_;
  VideoCodec codec_ = {};
  I420VideoFrame frame_;
};

void VideoEncoderSoftwareFallbackWrapperTest::UtilizeFallbackEncoder() {
  static const int kWidth = 320;
  static const int kHeight = 240;
  fallback_wrapper_.RegisterEncodeCompleteCallback(&callback_);
  EXPECT_EQ(&callback_, fake_encoder_.encode_complete_callback_);

  // Register with failing fake encoder. Should succeed with VP8 fallback.
  codec_.codecType = kVideoCodecVP8;
  codec_.maxFramerate = 30;
  codec_.width = kWidth;
  codec_.height = kHeight;
  fake_encoder_.init_encode_return_code_ = WEBRTC_VIDEO_CODEC_ERROR;
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            fallback_wrapper_.InitEncode(&codec_, 2, kMaxPayloadSize));
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.SetRates(300, 30));

  frame_.CreateEmptyFrame(kWidth, kHeight, kWidth, (kWidth + 1) / 2,
                          (kWidth + 1) / 2);
  memset(frame_.buffer(webrtc::kYPlane), 16,
         frame_.allocated_size(webrtc::kYPlane));
  memset(frame_.buffer(webrtc::kUPlane), 128,
         frame_.allocated_size(webrtc::kUPlane));
  memset(frame_.buffer(webrtc::kVPlane), 128,
         frame_.allocated_size(webrtc::kVPlane));

  std::vector<VideoFrameType> types(1, kKeyFrame);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            fallback_wrapper_.Encode(frame_, nullptr, &types));
  EXPECT_EQ(0, fake_encoder_.encode_count_);
  EXPECT_GT(callback_.callback_count_, 0);
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest, InitializesEncoder) {
  VideoCodec codec = {};
  fallback_wrapper_.InitEncode(&codec, 2, kMaxPayloadSize);
  EXPECT_EQ(1, fake_encoder_.init_encode_count_);
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest, CanUtilizeFallbackEncoder) {
  UtilizeFallbackEncoder();
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       InternalEncoderNotReleasedDuringFallback) {
  UtilizeFallbackEncoder();
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
  EXPECT_EQ(0, fake_encoder_.release_count_);
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       InternalEncoderNotEncodingDuringFallback) {
  UtilizeFallbackEncoder();
  EXPECT_EQ(0, fake_encoder_.encode_count_);

  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       CanRegisterCallbackWhileUsingFallbackEncoder) {
  UtilizeFallbackEncoder();
  // Registering an encode-complete callback should still work when fallback
  // encoder is being used.
  FakeEncodedImageCallback callback2;
  fallback_wrapper_.RegisterEncodeCompleteCallback(&callback2);
  EXPECT_EQ(&callback2, fake_encoder_.encode_complete_callback_);

  // Encoding a frame using the fallback should arrive at the new callback.
  std::vector<VideoFrameType> types(1, kKeyFrame);
  frame_.set_timestamp(frame_.timestamp() + 1000);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK,
            fallback_wrapper_.Encode(frame_, nullptr, &types));

  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       SetChannelParametersForwardedDuringFallback) {
  UtilizeFallbackEncoder();
  EXPECT_EQ(0, fake_encoder_.set_channel_parameters_count_);
  fallback_wrapper_.SetChannelParameters(1, 1);
  EXPECT_EQ(1, fake_encoder_.set_channel_parameters_count_);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       SetRatesForwardedDuringFallback) {
  UtilizeFallbackEncoder();
  EXPECT_EQ(1, fake_encoder_.set_rates_count_);
  fallback_wrapper_.SetRates(1, 1);
  EXPECT_EQ(2, fake_encoder_.set_rates_count_);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       OnDroppedFrameForwardedWithoutFallback) {
  fallback_wrapper_.OnDroppedFrame();
  EXPECT_EQ(1, fake_encoder_.on_dropped_frame_count_);
}

TEST_F(VideoEncoderSoftwareFallbackWrapperTest,
       OnDroppedFrameNotForwardedDuringFallback) {
  UtilizeFallbackEncoder();
  fallback_wrapper_.OnDroppedFrame();
  EXPECT_EQ(0, fake_encoder_.on_dropped_frame_count_);
  EXPECT_EQ(WEBRTC_VIDEO_CODEC_OK, fallback_wrapper_.Release());
}

}  // namespace webrtc
