#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <string.h>
#ifdef CONFIG_ARCH_POSIX
#include <nsi_main.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Track A: Feature vector headers & models
#include "test_samples.h"
#include "model_tree.h"
#include "model_forest.h"
#include "model_bayes.h"
#include "model_linear.h"

// Silence upstream emlearn header warnings
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#include "model_mlp.h"
#pragma GCC diagnostic pop

#ifdef __cplusplus
}
#endif

// Track B: LiteRT / TensorFlow Lite Micro
#ifdef CONFIG_TENSORFLOW_LITE_MICRO
#include <tensorflow/lite/micro/micro_interpreter.h>
#include <tensorflow/lite/micro/micro_mutable_op_resolver.h>
#include <tensorflow/lite/schema/schema_generated.h>
#include "model_cnn_data.h"

// Reserved tensor arena; full board memory fit has not been validated.
constexpr int kTensorArenaSize = 32 * 1024;
static uint8_t tensor_arena[kTensorArenaSize];
#endif

static const char* activity_names[] = {
    "WALKING", "WALKING_UPSTAIRS", "WALKING_DOWNSTAIRS",
    "SITTING", "STANDING", "LAYING"
};

void run_all_benchmarks(void) {
    printk("\n======================================================\n");
    printk("   Zephyr RTOS native_sim: Embedded HAR Benchmark     \n");
    printk("======================================================\n");
    printk("Target Ground Truth: %s (Class %d)\n\n",
           activity_names[SAMPLE_GROUND_TRUTH], SAMPLE_GROUND_TRUTH);

    // -----------------------------------------------------------
    // TRACK A: Handcrafted 561-Feature Models
    // -----------------------------------------------------------
    printk("--- Track A: Handcrafted Features (561-D) ---\n");

    // 1. Decision Tree
    int64_t t0 = k_uptime_get();
    int pred_tree = tree_model_predict(sample_features, NUM_FEATURES);
    int64_t dt_tree = k_uptime_get() - t0;
    printk("  [Decision Tree]       Pred: %-18s (Latency: %lld ms)\n",
           activity_names[pred_tree], dt_tree);

    // 2. Random Forest
    t0 = k_uptime_get();
    int pred_forest = forest_model_predict(sample_features, NUM_FEATURES);
    int64_t dt_forest = k_uptime_get() - t0;
    printk("  [Random Forest]       Pred: %-18s (Latency: %lld ms)\n",
           activity_names[pred_forest], dt_forest);

    // 3. Gaussian Naive Bayes
    t0 = k_uptime_get();
    int pred_nb = bayes_model_predict(sample_features, NUM_FEATURES);
    int64_t dt_nb = k_uptime_get() - t0;
    printk("  [Gaussian NB]         Pred: %-18s (Latency: %lld ms)\n",
           activity_names[pred_nb], dt_nb);

    // 4. Logistic / Linear Regression
    t0 = k_uptime_get();
    int pred_linear = linear_model_predict(sample_features, NUM_FEATURES);
    int64_t dt_linear = k_uptime_get() - t0;
    printk("  [Logistic/Linear]     Pred: %-18s (Latency: %lld ms)\n",
           activity_names[pred_linear], dt_linear);

    // 5. Multi-Layer Perceptron (MLP)
    t0 = k_uptime_get();
    int pred_mlp = mlp_model_predict(sample_features, NUM_FEATURES);
    int64_t dt_mlp = k_uptime_get() - t0;
    printk("  [MLP Neural Net]      Pred: %-18s (Latency: %lld ms)\n",
           activity_names[pred_mlp], dt_mlp);

    // -----------------------------------------------------------
    // TRACK B: End-to-End 1D-CNN (LiteRT / TFLM)
    // -----------------------------------------------------------
    printk("\n--- Track B: Raw Signals (128 steps x 9 channels) ---\n");

#ifdef CONFIG_TENSORFLOW_LITE_MICRO
    const tflite::Model* model = tflite::GetModel(cnn_model_tflite);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        printk("  [1D-CNN Error] Model schema version mismatch!\n");
        return;
    }

    // Increased capacity to hold all required ops
    tflite::MicroMutableOpResolver<12> resolver;
    resolver.AddQuantize();
    resolver.AddDequantize();
    resolver.AddConv2D();
    resolver.AddMaxPool2D();
    resolver.AddMean();
    resolver.AddFullyConnected();
    resolver.AddSoftmax();
    resolver.AddExpandDims();  // <-- Resolves 'EXPAND_DIMS' missing op
    resolver.AddReshape();     // <-- Common companion to ExpandDims
    resolver.AddRelu();
    resolver.AddPad();

    tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kTensorArenaSize);
    if (interpreter.AllocateTensors() != kTfLiteOk) {
        printk("  [1D-CNN Error] AllocateTensors() failed!\n");
        return;
    }

    TfLiteTensor* input = interpreter.input(0);
    memcpy(input->data.f, sample_raw_stream, RAW_SIGNAL_LEN * sizeof(float));

    t0 = k_uptime_get();
    if (interpreter.Invoke() != kTfLiteOk) {
        printk("  [1D-CNN Error] Invoke() failed!\n");
        return;
    }
    int64_t dt_cnn = k_uptime_get() - t0;

    TfLiteTensor* output = interpreter.output(0);
    int cnn_pred = 0;
    float max_prob = -1e9f;

    for (int i = 0; i < 6; i++) {
        float prob = output->data.f[i];
        if (prob > max_prob) {
            max_prob = prob;
            cnn_pred = i;
        }
    }

    printk("  [1D-CNN LiteRT]       Pred: %-18s (Latency: %lld ms)\n",
           activity_names[cnn_pred], dt_cnn);
#else
    printk("  [1D-CNN LiteRT]       Disabled (Set CONFIG_TENSORFLOW_LITE_MICRO=y in prj.conf)\n");
#endif

    printk("======================================================\n");
}

int main(void) {
    run_all_benchmarks();
#ifdef CONFIG_ARCH_POSIX
    nsi_exit(0);
#endif
    return 0;
}
