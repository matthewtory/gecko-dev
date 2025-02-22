/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */
#ifndef MOCKCUBEB_H_
#define MOCKCUBEB_H_

#include "AudioDeviceInfo.h"
#include "AudioGenerator.h"
#include "AudioVerifier.h"
#include "MediaEventSource.h"
#include "nsTArray.h"

#include <thread>
#include <atomic>
#include <chrono>

namespace mozilla {
const uint32_t NUM_OF_CHANNELS = 2;

struct cubeb_ops {
  int (*init)(cubeb** context, char const* context_name);
  char const* (*get_backend_id)(cubeb* context);
  int (*get_max_channel_count)(cubeb* context, uint32_t* max_channels);
  int (*get_min_latency)(cubeb* context, cubeb_stream_params params,
                         uint32_t* latency_ms);
  int (*get_preferred_sample_rate)(cubeb* context, uint32_t* rate);
  int (*enumerate_devices)(cubeb* context, cubeb_device_type type,
                           cubeb_device_collection* collection);
  int (*device_collection_destroy)(cubeb* context,
                                   cubeb_device_collection* collection);
  void (*destroy)(cubeb* context);
  int (*stream_init)(cubeb* context, cubeb_stream** stream,
                     char const* stream_name, cubeb_devid input_device,
                     cubeb_stream_params* input_stream_params,
                     cubeb_devid output_device,
                     cubeb_stream_params* output_stream_params,
                     unsigned int latency, cubeb_data_callback data_callback,
                     cubeb_state_callback state_callback, void* user_ptr);
  void (*stream_destroy)(cubeb_stream* stream);
  int (*stream_start)(cubeb_stream* stream);
  int (*stream_stop)(cubeb_stream* stream);
  int (*stream_reset_default_device)(cubeb_stream* stream);
  int (*stream_get_position)(cubeb_stream* stream, uint64_t* position);
  int (*stream_get_latency)(cubeb_stream* stream, uint32_t* latency);
  int (*stream_set_volume)(cubeb_stream* stream, float volumes);
  int (*stream_set_panning)(cubeb_stream* stream, float panning);
  int (*stream_get_current_device)(cubeb_stream* stream,
                                   cubeb_device** const device);
  int (*stream_device_destroy)(cubeb_stream* stream, cubeb_device* device);
  int (*stream_register_device_changed_callback)(
      cubeb_stream* stream,
      cubeb_device_changed_callback device_changed_callback);
  int (*register_device_collection_changed)(
      cubeb* context, cubeb_device_type devtype,
      cubeb_device_collection_changed_callback callback, void* user_ptr);
};

// Keep those and the struct definition in sync with cubeb.h and
// cubeb-internal.h
void cubeb_mock_destroy(cubeb* context);
static int cubeb_mock_enumerate_devices(cubeb* context, cubeb_device_type type,
                                        cubeb_device_collection* out);

static int cubeb_mock_device_collection_destroy(
    cubeb* context, cubeb_device_collection* collection);

static int cubeb_mock_register_device_collection_changed(
    cubeb* context, cubeb_device_type devtype,
    cubeb_device_collection_changed_callback callback, void* user_ptr);

static int cubeb_mock_stream_init(
    cubeb* context, cubeb_stream** stream, char const* stream_name,
    cubeb_devid input_device, cubeb_stream_params* input_stream_params,
    cubeb_devid output_device, cubeb_stream_params* output_stream_params,
    unsigned int latency, cubeb_data_callback data_callback,
    cubeb_state_callback state_callback, void* user_ptr);

static int cubeb_mock_stream_start(cubeb_stream* stream);

static int cubeb_mock_stream_stop(cubeb_stream* stream);

static void cubeb_mock_stream_destroy(cubeb_stream* stream);

static char const* cubeb_mock_get_backend_id(cubeb* context);

static int cubeb_mock_stream_set_volume(cubeb_stream* stream, float volume);

static int cubeb_mock_get_min_latency(cubeb* context,
                                      cubeb_stream_params params,
                                      uint32_t* latency_ms);

static int cubeb_mock_get_max_channel_count(cubeb* context,
                                            uint32_t* max_channels);

// Mock cubeb impl, only supports device enumeration for now.
cubeb_ops const mock_ops = {
    /*.init =*/NULL,
    /*.get_backend_id =*/cubeb_mock_get_backend_id,
    /*.get_max_channel_count =*/cubeb_mock_get_max_channel_count,
    /*.get_min_latency =*/cubeb_mock_get_min_latency,
    /*.get_preferred_sample_rate =*/NULL,
    /*.enumerate_devices =*/cubeb_mock_enumerate_devices,
    /*.device_collection_destroy =*/cubeb_mock_device_collection_destroy,
    /*.destroy =*/cubeb_mock_destroy,
    /*.stream_init =*/cubeb_mock_stream_init,
    /*.stream_destroy =*/cubeb_mock_stream_destroy,
    /*.stream_start =*/cubeb_mock_stream_start,
    /*.stream_stop =*/cubeb_mock_stream_stop,
    /*.stream_reset_default_device =*/NULL,
    /*.stream_get_position =*/NULL,
    /*.stream_get_latency =*/NULL,
    /*.stream_set_volume =*/cubeb_mock_stream_set_volume,
    /*.stream_set_panning =*/NULL,
    /*.stream_get_current_device =*/NULL,
    /*.stream_device_destroy =*/NULL,
    /*.stream_register_device_changed_callback =*/NULL,
    /*.register_device_collection_changed =*/

    cubeb_mock_register_device_collection_changed};

// Represents the fake cubeb_stream. The context instance is needed to
// provide access on cubeb_ops struct.
class MockCubebStream {
 public:
  MockCubebStream(cubeb* aContext, cubeb_devid aInputDevice,
                  cubeb_stream_params* aInputStreamParams,
                  cubeb_devid aOutputDevice,
                  cubeb_stream_params* aOutputStreamParams,
                  cubeb_data_callback aDataCallback,
                  cubeb_state_callback aStateCallback, void* aUserPtr)
      : context(aContext),
        mHasInput(aInputStreamParams),
        mHasOutput(aOutputStreamParams),
        mDataCallback(aDataCallback),
        mStateCallback(aStateCallback),
        mUserPtr(aUserPtr),
        mInputDeviceID(aInputDevice),
        mOutputDeviceID(aOutputDevice),
        mAudioGenerator(NUM_OF_CHANNELS,
                        aInputStreamParams ? aInputStreamParams->rate
                                           : aOutputStreamParams->rate,
                        100 /* aFrequency */),
        mAudioVerifier(aInputStreamParams ? aInputStreamParams->rate
                                          : aOutputStreamParams->rate,
                       100 /* aFrequency */) {
    if (aInputStreamParams) {
      mInputParams = *aInputStreamParams;
    }
    if (aOutputStreamParams) {
      mOutputParams = *aOutputStreamParams;
    }
  }

  ~MockCubebStream() = default;

  int Start();
  int Stop();

  cubeb_devid GetInputDeviceID() const { return mInputDeviceID; }
  cubeb_devid GetOutputDeviceID() const { return mOutputDeviceID; }

  uint32_t InputChannels() const { return mAudioGenerator.mChannels; }
  uint32_t InputSampleRate() const { return mAudioGenerator.mSampleRate; }
  uint32_t InputFrequency() const { return mAudioGenerator.mFrequency; }

  void SetDriftFactor(float aDriftFactor) { mDriftFactor = aDriftFactor; }
  void ForceError() { mForceErrorState = true; }

  MediaEventSource<uint32_t>& FramesProcessedEvent() {
    return mFramesProcessedEvent;
  }

  MediaEventSource<Tuple<uint64_t, float, uint32_t>>&
  OutputVerificationEvent() {
    return mOutputVerificationEvent;
  }

  MediaEventSource<void>& ErrorForcedEvent() { return mErrorForcedEvent; }

  void Process10Ms() {
    if (mStreamStop) {
      return;
    }

    uint32_t rate = mHasOutput ? mOutputParams.rate : mInputParams.rate;
    const long nrFrames =
        static_cast<long>(static_cast<float>(rate * 10) * mDriftFactor) /
        PR_MSEC_PER_SEC;
    if (mInputParams.rate) {
      mAudioGenerator.GenerateInterleaved(mInputBuffer, nrFrames);
    }
    cubeb_stream* stream = reinterpret_cast<cubeb_stream*>(this);
    const long outframes =
        mDataCallback(stream, mUserPtr, mHasInput ? mInputBuffer : nullptr,
                      mHasOutput ? mOutputBuffer : nullptr, nrFrames);

    mAudioVerifier.AppendDataInterleaved(mOutputBuffer, outframes,
                                         NUM_OF_CHANNELS);

    if (mAudioVerifier.PreSilenceEnded()) {
      mFramesProcessedEvent.Notify(outframes);
    }

    if (outframes < nrFrames) {
      mStateCallback(stream, mUserPtr, CUBEB_STATE_DRAINED);
      mStreamStop = true;
      return;
    }
    if (mForceErrorState) {
      mForceErrorState = false;
      mStateCallback(stream, mUserPtr, CUBEB_STATE_ERROR);
      mErrorForcedEvent.Notify();
      mStreamStop = true;
      return;
    }
  }

 public:
  cubeb* context = nullptr;

  const bool mHasInput;
  const bool mHasOutput;

 private:
  // Signal to the audio thread that stream is stopped.
  std::atomic_bool mStreamStop{true};
  // The audio buffer used on data callback.
  AudioDataValue mOutputBuffer[NUM_OF_CHANNELS * 1920] = {};
  AudioDataValue mInputBuffer[NUM_OF_CHANNELS * 1920] = {};
  // The audio callback
  cubeb_data_callback mDataCallback = nullptr;
  // The stream state callback
  cubeb_state_callback mStateCallback = nullptr;
  // Stream's user data
  void* mUserPtr = nullptr;
  // The stream params
  cubeb_stream_params mOutputParams = {};
  cubeb_stream_params mInputParams = {};
  /* Device IDs */
  cubeb_devid mInputDeviceID;
  cubeb_devid mOutputDeviceID;

  std::atomic<float> mDriftFactor{1.0};
  std::atomic_bool mFastMode{false};
  std::atomic_bool mForceErrorState{false};
  AudioGenerator<AudioDataValue> mAudioGenerator;
  AudioVerifier<AudioDataValue> mAudioVerifier;

  MediaEventProducer<uint32_t> mFramesProcessedEvent;
  MediaEventProducer<Tuple<uint64_t, float, uint32_t>> mOutputVerificationEvent;
  MediaEventProducer<void> mErrorForcedEvent;
};

// This class has two facets: it is both a fake cubeb backend that is intended
// to be used for testing, and passed to Gecko code that expects a normal
// backend, but is also controllable by the test code to decide what the backend
// should do, depending on what is being tested.
class MockCubeb {
 public:
  MockCubeb() : ops(&mock_ops) {}
  ~MockCubeb() { MOZ_ASSERT(!mFakeAudioThread); };
  // Cubeb backend implementation
  // This allows passing this class as a cubeb* instance.
  cubeb* AsCubebContext() { return reinterpret_cast<cubeb*>(this); }
  // Fill in the collection parameter with all devices of aType.
  int EnumerateDevices(cubeb_device_type aType,
                       cubeb_device_collection* collection) {
#ifdef ANDROID
    EXPECT_TRUE(false) << "This is not to be called on Android.";
#endif
    size_t count = 0;
    if (aType & CUBEB_DEVICE_TYPE_INPUT) {
      count += mInputDevices.Length();
    }
    if (aType & CUBEB_DEVICE_TYPE_OUTPUT) {
      count += mOutputDevices.Length();
    }
    collection->device = new cubeb_device_info[count];
    collection->count = count;

    uint32_t collection_index = 0;
    if (aType & CUBEB_DEVICE_TYPE_INPUT) {
      for (auto& device : mInputDevices) {
        collection->device[collection_index] = device;
        collection_index++;
      }
    }
    if (aType & CUBEB_DEVICE_TYPE_OUTPUT) {
      for (auto& device : mOutputDevices) {
        collection->device[collection_index] = device;
        collection_index++;
      }
    }

    return CUBEB_OK;
  }

  // For a given device type, add a callback, called with a user pointer, when
  // the device collection for this backend changes (i.e. a device has been
  // removed or added).
  int RegisterDeviceCollectionChangeCallback(
      cubeb_device_type aDevType,
      cubeb_device_collection_changed_callback aCallback, void* aUserPtr) {
    if (!mSupportsDeviceCollectionChangedCallback) {
      return CUBEB_ERROR;
    }

    if (aDevType & CUBEB_DEVICE_TYPE_INPUT) {
      mInputDeviceCollectionChangeCallback = aCallback;
      mInputDeviceCollectionChangeUserPtr = aUserPtr;
    }
    if (aDevType & CUBEB_DEVICE_TYPE_OUTPUT) {
      mOutputDeviceCollectionChangeCallback = aCallback;
      mOutputDeviceCollectionChangeUserPtr = aUserPtr;
    }

    return CUBEB_OK;
  }

  // Control API

  // Add an input or output device to this backend. This calls the device
  // collection invalidation callback if needed.
  void AddDevice(cubeb_device_info aDevice) {
    if (aDevice.type == CUBEB_DEVICE_TYPE_INPUT) {
      mInputDevices.AppendElement(aDevice);
    } else if (aDevice.type == CUBEB_DEVICE_TYPE_OUTPUT) {
      mOutputDevices.AppendElement(aDevice);
    } else {
      MOZ_CRASH("bad device type when adding a device in mock cubeb backend");
    }

    bool isInput = aDevice.type & CUBEB_DEVICE_TYPE_INPUT;
    if (isInput && mInputDeviceCollectionChangeCallback) {
      mInputDeviceCollectionChangeCallback(AsCubebContext(),
                                           mInputDeviceCollectionChangeUserPtr);
    }
    if (!isInput && mOutputDeviceCollectionChangeCallback) {
      mOutputDeviceCollectionChangeCallback(
          AsCubebContext(), mOutputDeviceCollectionChangeUserPtr);
    }
  }
  // Remove a specific input or output device to this backend, returns true if
  // a device was removed. This calls the device collection invalidation
  // callback if needed.
  bool RemoveDevice(cubeb_devid aId) {
    bool foundInput = false;
    bool foundOutput = false;
    mInputDevices.RemoveElementsBy(
        [aId, &foundInput](cubeb_device_info& aDeviceInfo) {
          bool foundThisTime = aDeviceInfo.devid == aId;
          foundInput |= foundThisTime;
          return foundThisTime;
        });
    mOutputDevices.RemoveElementsBy(
        [aId, &foundOutput](cubeb_device_info& aDeviceInfo) {
          bool foundThisTime = aDeviceInfo.devid == aId;
          foundOutput |= foundThisTime;
          return foundThisTime;
        });

    if (foundInput && mInputDeviceCollectionChangeCallback) {
      mInputDeviceCollectionChangeCallback(AsCubebContext(),
                                           mInputDeviceCollectionChangeUserPtr);
    }
    if (foundOutput && mOutputDeviceCollectionChangeCallback) {
      mOutputDeviceCollectionChangeCallback(
          AsCubebContext(), mOutputDeviceCollectionChangeUserPtr);
    }
    // If the device removed was a default device, set another device as the
    // default, if there are still devices available.
    bool foundDefault = false;
    for (uint32_t i = 0; i < mInputDevices.Length(); i++) {
      foundDefault |= mInputDevices[i].preferred != CUBEB_DEVICE_PREF_NONE;
    }

    if (!foundDefault) {
      if (!mInputDevices.IsEmpty()) {
        mInputDevices[mInputDevices.Length() - 1].preferred =
            CUBEB_DEVICE_PREF_ALL;
      }
    }

    foundDefault = false;
    for (uint32_t i = 0; i < mOutputDevices.Length(); i++) {
      foundDefault |= mOutputDevices[i].preferred != CUBEB_DEVICE_PREF_NONE;
    }

    if (!foundDefault) {
      if (!mOutputDevices.IsEmpty()) {
        mOutputDevices[mOutputDevices.Length() - 1].preferred =
            CUBEB_DEVICE_PREF_ALL;
      }
    }

    return foundInput | foundOutput;
  }
  // Remove all input or output devices from this backend, without calling the
  // callback. This is meant to clean up in between tests.
  void ClearDevices(cubeb_device_type aType) {
    mInputDevices.Clear();
    mOutputDevices.Clear();
  }

  // This allows simulating a backend that does not support setting a device
  // collection invalidation callback, to be able to test the fallback path.
  void SetSupportDeviceChangeCallback(bool aSupports) {
    mSupportsDeviceCollectionChangedCallback = aSupports;
  }

  int StreamInit(cubeb* aContext, cubeb_stream** aStream,
                 cubeb_devid aInputDevice,
                 cubeb_stream_params* aInputStreamParams,
                 cubeb_devid aOutputDevice,
                 cubeb_stream_params* aOutputStreamParams,
                 cubeb_data_callback aDataCallback,
                 cubeb_state_callback aStateCallback, void* aUserPtr) {
    MockCubebStream* mockStream = new MockCubebStream(
        aContext, aInputDevice, aInputStreamParams, aOutputDevice,
        aOutputStreamParams, aDataCallback, aStateCallback, aUserPtr);
    *aStream = reinterpret_cast<cubeb_stream*>(mockStream);
    mStreamInitEvent.Notify(mockStream);
    return CUBEB_OK;
  }

  void StreamDestroy(cubeb_stream* aStream) {
    mStreamDestroyEvent.Notify();
    MockCubebStream* mockStream = reinterpret_cast<MockCubebStream*>(aStream);
    delete mockStream;
  }

  void GoFaster() { mFastMode = true; }
  void DontGoFaster() { mFastMode = false; }

  MediaEventSource<MockCubebStream*>& StreamInitEvent() {
    return mStreamInitEvent;
  }
  MediaEventSource<void>& StreamDestroyEvent() { return mStreamDestroyEvent; }

  // MockCubeb specific API
  void StartStream(MockCubebStream* aStream) {
    auto streams = mLiveStreams.Lock();
    MOZ_ASSERT(!streams->Contains(aStream));
    streams->AppendElement(aStream);
    if (!mFakeAudioThread) {
      mFakeAudioThread = WrapUnique(new std::thread(ThreadFunction_s, this));
    }
  }

  void StopStream(MockCubebStream* aStream) {
    UniquePtr<std::thread> audioThread;
    {
      auto streams = mLiveStreams.Lock();
      MOZ_ASSERT(streams->Contains(aStream));
      streams->RemoveElement(aStream);
      MOZ_ASSERT(mFakeAudioThread);
      if (streams->IsEmpty()) {
        audioThread = std::move(mFakeAudioThread);
      }
    }
    if (audioThread) {
      audioThread->join();
    }
  }

  // Simulates the audio thread. The thread is created at Start and destroyed
  // at Stop. At next StreamStart a new thread is created.
  static void ThreadFunction_s(MockCubeb* aContext) {
    aContext->ThreadFunction();
  }

  void ThreadFunction() {
    while (true) {
      {
        auto streams = mLiveStreams.Lock();
        for (auto& stream : *streams) {
          stream->Process10Ms();
        }
        if (streams->IsEmpty()) {
          break;
        }
      }
      std::this_thread::sleep_for(
          std::chrono::microseconds(mFastMode ? 0 : 10 * PR_USEC_PER_MSEC));
    }
  }

 private:
  // This needs to have the exact same memory layout as a real cubeb backend.
  // It's very important for this `ops` member to be the very first member of
  // the class, and to not have any virtual members (to avoid having a
  // vtable).
  const cubeb_ops* ops;
  // The callback to call when the device list has been changed.
  cubeb_device_collection_changed_callback
      mInputDeviceCollectionChangeCallback = nullptr;
  cubeb_device_collection_changed_callback
      mOutputDeviceCollectionChangeCallback = nullptr;
  // The pointer to pass in the callback.
  void* mInputDeviceCollectionChangeUserPtr = nullptr;
  void* mOutputDeviceCollectionChangeUserPtr = nullptr;
  void* mUserPtr = nullptr;
  // Whether or not this backend supports device collection change
  // notification via a system callback. If not, Gecko is expected to re-query
  // the list every time.
  bool mSupportsDeviceCollectionChangedCallback = true;
  // Our input and output devices.
  nsTArray<cubeb_device_info> mInputDevices;
  nsTArray<cubeb_device_info> mOutputDevices;

  // The streams that are currently running.
  DataMutex<nsTArray<MockCubebStream*>> mLiveStreams{"MockCubeb::mLiveStreams"};
  // Thread that simulates the audio thread, shared across MockCubebStreams to
  // avoid unintended drift. This is set together with mLiveStreams, under the
  // mLiveStreams DataMutex.
  UniquePtr<std::thread> mFakeAudioThread;
  // Whether to run the fake audio thread in fast mode, not caring about wall
  // clock time. false is default and means data is processed every 10ms. When
  // true we sleep(0) between iterations instead of 10ms.
  std::atomic<bool> mFastMode{false};

  MediaEventProducer<MockCubebStream*> mStreamInitEvent;
  MediaEventProducer<void> mStreamDestroyEvent;
};

int MockCubebStream::Start() {
  mStreamStop = false;
  reinterpret_cast<MockCubeb*>(context)->StartStream(this);
  cubeb_stream* stream = reinterpret_cast<cubeb_stream*>(this);
  mStateCallback(stream, mUserPtr, CUBEB_STATE_STARTED);
  return CUBEB_OK;
}

int MockCubebStream::Stop() {
  mStreamStop = true;
  mOutputVerificationEvent.Notify(MakeTuple(
      mAudioVerifier.PreSilenceSamples(), mAudioVerifier.EstimatedFreq(),
      mAudioVerifier.CountDiscontinuities()));
  reinterpret_cast<MockCubeb*>(context)->StopStream(this);
  cubeb_stream* stream = reinterpret_cast<cubeb_stream*>(this);
  mStateCallback(stream, mUserPtr, CUBEB_STATE_STOPPED);
  return CUBEB_OK;
}

void cubeb_mock_destroy(cubeb* context) {
  delete reinterpret_cast<MockCubeb*>(context);
}

int cubeb_mock_enumerate_devices(cubeb* context, cubeb_device_type type,
                                 cubeb_device_collection* out) {
  MockCubeb* mock = reinterpret_cast<MockCubeb*>(context);
  return mock->EnumerateDevices(type, out);
}

int cubeb_mock_device_collection_destroy(cubeb* context,
                                         cubeb_device_collection* collection) {
  delete[] collection->device;
  return CUBEB_OK;
}

int cubeb_mock_register_device_collection_changed(
    cubeb* context, cubeb_device_type devtype,
    cubeb_device_collection_changed_callback callback, void* user_ptr) {
  MockCubeb* mock = reinterpret_cast<MockCubeb*>(context);
  return mock->RegisterDeviceCollectionChangeCallback(devtype, callback,
                                                      user_ptr);
}

int cubeb_mock_stream_init(
    cubeb* context, cubeb_stream** stream, char const* stream_name,
    cubeb_devid input_device, cubeb_stream_params* input_stream_params,
    cubeb_devid output_device, cubeb_stream_params* output_stream_params,
    unsigned int latency, cubeb_data_callback data_callback,
    cubeb_state_callback state_callback, void* user_ptr) {
  MockCubeb* mock = reinterpret_cast<MockCubeb*>(context);
  return mock->StreamInit(context, stream, input_device, input_stream_params,
                          output_device, output_stream_params, data_callback,
                          state_callback, user_ptr);
}

int cubeb_mock_stream_start(cubeb_stream* stream) {
  MockCubebStream* mockStream = reinterpret_cast<MockCubebStream*>(stream);
  return mockStream->Start();
}

int cubeb_mock_stream_stop(cubeb_stream* stream) {
  MockCubebStream* mockStream = reinterpret_cast<MockCubebStream*>(stream);
  return mockStream->Stop();
}

void cubeb_mock_stream_destroy(cubeb_stream* stream) {
  MockCubebStream* mockStream = reinterpret_cast<MockCubebStream*>(stream);
  MockCubeb* mock = reinterpret_cast<MockCubeb*>(mockStream->context);
  return mock->StreamDestroy(stream);
}

static char const* cubeb_mock_get_backend_id(cubeb* context) {
#if defined(XP_MACOSX)
  return "audiounit";
#elif defined(XP_WIN)
  return "wasapi";
#elif defined(ANDROID)
  return "opensl";
#elif defined(__OpenBSD__)
  return "sndio";
#else
  return "pulse";
#endif
}

static int cubeb_mock_stream_set_volume(cubeb_stream* stream, float volume) {
  return CUBEB_OK;
}

int cubeb_mock_get_min_latency(cubeb* context, cubeb_stream_params params,
                               uint32_t* latency_ms) {
  *latency_ms = 10;
  return CUBEB_OK;
}

int cubeb_mock_get_max_channel_count(cubeb* context, uint32_t* max_channels) {
  *max_channels = NUM_OF_CHANNELS;
  return CUBEB_OK;
}

void PrintDevice(cubeb_device_info aInfo) {
  printf(
      "id: %zu\n"
      "device_id: %s\n"
      "friendly_name: %s\n"
      "group_id: %s\n"
      "vendor_name: %s\n"
      "type: %d\n"
      "state: %d\n"
      "preferred: %d\n"
      "format: %d\n"
      "default_format: %d\n"
      "max_channels: %d\n"
      "default_rate: %d\n"
      "max_rate: %d\n"
      "min_rate: %d\n"
      "latency_lo: %d\n"
      "latency_hi: %d\n",
      reinterpret_cast<uintptr_t>(aInfo.devid), aInfo.device_id,
      aInfo.friendly_name, aInfo.group_id, aInfo.vendor_name, aInfo.type,
      aInfo.state, aInfo.preferred, aInfo.format, aInfo.default_format,
      aInfo.max_channels, aInfo.default_rate, aInfo.max_rate, aInfo.min_rate,
      aInfo.latency_lo, aInfo.latency_hi);
}

void PrintDevice(AudioDeviceInfo* aInfo) {
  cubeb_devid id;
  nsString name;
  nsString groupid;
  nsString vendor;
  uint16_t type;
  uint16_t state;
  uint16_t preferred;
  uint16_t supportedFormat;
  uint16_t defaultFormat;
  uint32_t maxChannels;
  uint32_t defaultRate;
  uint32_t maxRate;
  uint32_t minRate;
  uint32_t maxLatency;
  uint32_t minLatency;

  id = aInfo->DeviceID();
  aInfo->GetName(name);
  aInfo->GetGroupId(groupid);
  aInfo->GetVendor(vendor);
  aInfo->GetType(&type);
  aInfo->GetState(&state);
  aInfo->GetPreferred(&preferred);
  aInfo->GetSupportedFormat(&supportedFormat);
  aInfo->GetDefaultFormat(&defaultFormat);
  aInfo->GetMaxChannels(&maxChannels);
  aInfo->GetDefaultRate(&defaultRate);
  aInfo->GetMaxRate(&maxRate);
  aInfo->GetMinRate(&minRate);
  aInfo->GetMinLatency(&minLatency);
  aInfo->GetMaxLatency(&maxLatency);

  printf(
      "device id: %zu\n"
      "friendly_name: %s\n"
      "group_id: %s\n"
      "vendor_name: %s\n"
      "type: %d\n"
      "state: %d\n"
      "preferred: %d\n"
      "format: %d\n"
      "default_format: %d\n"
      "max_channels: %d\n"
      "default_rate: %d\n"
      "max_rate: %d\n"
      "min_rate: %d\n"
      "latency_lo: %d\n"
      "latency_hi: %d\n",
      reinterpret_cast<uintptr_t>(id), NS_LossyConvertUTF16toASCII(name).get(),
      NS_LossyConvertUTF16toASCII(groupid).get(),
      NS_LossyConvertUTF16toASCII(vendor).get(), type, state, preferred,
      supportedFormat, defaultFormat, maxChannels, defaultRate, maxRate,
      minRate, minLatency, maxLatency);
}

cubeb_device_info DeviceTemplate(cubeb_devid aId, cubeb_device_type aType,
                                 const char* name) {
  // A fake input device
  cubeb_device_info device;
  device.devid = aId;
  device.device_id = "nice name";
  device.friendly_name = name;
  device.group_id = "the physical device";
  device.vendor_name = "mozilla";
  device.type = aType;
  device.state = CUBEB_DEVICE_STATE_ENABLED;
  device.preferred = CUBEB_DEVICE_PREF_NONE;
  device.format = CUBEB_DEVICE_FMT_F32NE;
  device.default_format = CUBEB_DEVICE_FMT_F32NE;
  device.max_channels = 2;
  device.default_rate = 44100;
  device.max_rate = 44100;
  device.min_rate = 16000;
  device.latency_lo = 256;
  device.latency_hi = 1024;

  return device;
}

cubeb_device_info DeviceTemplate(cubeb_devid aId, cubeb_device_type aType) {
  return DeviceTemplate(aId, aType, "nice name");
}

void AddDevices(MockCubeb* mock, uint32_t device_count,
                cubeb_device_type deviceType) {
  mock->ClearDevices(deviceType);
  // Add a few input devices (almost all the same but it does not really
  // matter as long as they have distinct IDs and only one is the default
  // devices)
  for (uintptr_t i = 0; i < device_count; i++) {
    cubeb_device_info device =
        DeviceTemplate(reinterpret_cast<void*>(i + 1), deviceType);
    // Make it so that the last device is the default input device.
    if (i == device_count - 1) {
      device.preferred = CUBEB_DEVICE_PREF_ALL;
    }
    mock->AddDevice(device);
  }
}
}  // namespace mozilla

#endif  // MOCKCUBEB_H_
