// Executable diagnostic extractions, NOT a claim the original iOS app ran.
// The offending reads reproduce ViewController.m lines 766-768 and 890-892.
#include <cstdlib>
#include <cstring>
#include <string>
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  if (std::string(argv[1]) == "insert") {
    auto *model = static_cast<float *>(std::calloc(4 * 12, sizeof(float)));
    int modelID = -1;
    volatile float currentConfidence = model[modelID * 12 + 10];
    std::free(model);
    return currentConfidence == 0 ? 0 : 3;
  }
  if (std::string(argv[1]) == "removal") {
    auto *rgba = static_cast<float *>(std::calloc(4, sizeof(float)));
    rgba[0] = 4;
    volatile float id = 0;
    for (int k = 1; k <= int(rgba[0]); k++)
      id = rgba[k];
    std::free(rgba);
    return id == 0 ? 0 : 3;
  }
  return 2;
}
