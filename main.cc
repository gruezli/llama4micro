#include <string>
#include <vector>

#include "libs/base/filesystem.h"
#include "libs/base/gpio.h"
#include "libs/base/led.h"
#include "libs/base/timer.h"
#include "third_party/freertos_kernel/include/FreeRTOS.h"
#include "third_party/freertos_kernel/include/task.h"

#include "llama2.h"

using namespace coralmicro;

// Llama model data paths.
const char* kLlamaModelPath = "/data/stories15M_q80.bin";
const char* kLlamaTokenizerPath = "/data/tokenizer.bin";

// Llama model inference parameters.
const float kTemperature = 1.0f;
const float kTopP = 0.9f;
const int kSteps = 256;
const char* kPrompt = "";

// Llama model data structures.
Transformer transformer;
std::vector<uint8_t>* llama_model_buffer;
int group_size;
int steps = kSteps;
Tokenizer tokenizer;
std::vector<uint8_t>* llama_tokenizer_buffer;
Sampler sampler;

// Debounce interval for the button interrupt.
const uint64_t kButtonDebounceUs = 50000;

// Loads the Llama model and tokenizer into memory and sets up data structures.
void LoadLlamaModel() {
  printf(">>> Loading Llama model %s...\n", kLlamaModelPath);
  int64_t timer_start = TimerMillis();

  llama_model_buffer = new std::vector<uint8_t>();
  build_transformer(&transformer, kLlamaModelPath, llama_model_buffer,
                    &group_size);
  if (steps == 0 || steps > transformer.config.seq_len) {
    steps = transformer.config.seq_len;
  }

  llama_tokenizer_buffer = new std::vector<uint8_t>();
  build_tokenizer(&tokenizer, kLlamaTokenizerPath, llama_tokenizer_buffer,
                  transformer.config.vocab_size);

  unsigned long long rng_seed = xTaskGetTickCount();
  build_sampler(&sampler, transformer.config.vocab_size, kTemperature, kTopP,
                rng_seed);

  int64_t timer_stop = TimerMillis();
  float timer_s = (timer_stop - timer_start) / 1000.0f;
  printf(">>> Llama model loading took %.2f s\n", timer_s);
}

// Frees the memory associated with the Llama model.
void UnloadLlamaModel() {
  printf(">>> Unloading Llama model...\n");

  free_sampler(&sampler);
  free_tokenizer(&tokenizer);
  free_transformer(&transformer);

  delete llama_tokenizer_buffer;
  delete llama_model_buffer;
}

// Generates a story beginning with the specified prompt.
void TellStory(std::string prompt) {
  printf(">>> Generating tokens...\n");

  float tokens_s;
  generate(&transformer, &tokenizer, &sampler, prompt.c_str(), steps,
           group_size, &tokens_s);

  printf(">>> Averaged %.2f tokens/s\n", tokens_s);
}

extern "C" [[noreturn]] void app_main(void* param) {
  (void)param;

  // Set up the button interrupt.
  GpioConfigureInterrupt(
      Gpio::kUserButton, GpioInterruptMode::kIntModeFalling,
      [handle = xTaskGetCurrentTaskHandle()]() { xTaskResumeFromISR(handle); },
      kButtonDebounceUs);

  // Load the model while showing the status LED.
  LedSet(Led::kStatus, true);
  LedSet(Led::kUser, false);
  LoadLlamaModel();

  while (true) {
    // Wait for a button press while showing the user LED.
    LedSet(Led::kStatus, false);
    LedSet(Led::kUser, true);
    vTaskSuspend(nullptr);
    // Continuing here after the button interrupt.

    // Tell a story while showing the status LED.
    LedSet(Led::kStatus, true);
    LedSet(Led::kUser, false);
    TellStory(kPrompt);
  }

  // Unreachable in regular operation. The model stays in memory.
  UnloadLlamaModel();
}
