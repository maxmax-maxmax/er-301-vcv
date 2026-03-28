#include "plugin.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <cstring>

// ER-301 headers
extern "C"
{
#include <od/config.h>
#include <hal/constants.h>
#include <hal/channels.h>
#include <hal/pump.h>
#include <hal/audio.h>
#include <hal/heap.h>
#include <hal/timing.h>
#include <hal/uart.h>
#include <hal/card.h>
#include <hal/log.h>
#include <hal/gpio.h>
#include <hal/events.h>
#include <hal/usb.h>
#include <hal/encoder.h>
#include <hal/pwm.h>
#include <hal/adc.h>
#include <hal/modulation.h>
#include <hal/display.h>
#include <hal/rng.h>
#include <emu/tls.h>

  // Custom PWM readback for VCV lights
  void Pwm_getLed(int channel, float *red, float *green);
}

#include <od/glue/AppInterpreter.h>
#include <od/extras/Random.h>
#include <od/AudioThread.h>

struct ER301Module : Module
{
  enum InputIds
  {
    INPUT_IN1_PORT,
    INPUT_IN2_PORT,
    INPUT_IN3_PORT,
    INPUT_IN4_PORT,
    INPUT_A1_PORT,
    INPUT_B1_PORT,
    INPUT_C1_PORT,
    INPUT_D1_PORT,
    INPUT_A2_PORT,
    INPUT_B2_PORT,
    INPUT_C2_PORT,
    INPUT_D2_PORT,
    INPUT_A3_PORT,
    INPUT_B3_PORT,
    INPUT_C3_PORT,
    INPUT_D3_PORT,
    INPUT_G1_PORT,
    INPUT_G2_PORT,
    INPUT_G3_PORT,
    INPUT_G4_PORT,
    NUM_INPUTS
  };

  enum OutputIds
  {
    OUTPUT_OUT1,
    OUTPUT_OUT2,
    OUTPUT_OUT3,
    OUTPUT_OUT4,
    NUM_OUTPUTS
  };

  enum ParamIds
  {
    NUM_PARAMS
  };

  enum LightIds
  {
    // GPIO LEDs (single color)
    LIGHT_DIAL1,
    LIGHT_DIAL2,
    LIGHT_IO,
    LIGHT_SAFE,
    LIGHT_OUT1,
    LIGHT_OUT2,
    LIGHT_OUT3,
    LIGHT_OUT4,
    LIGHT_LINK12,
    LIGHT_LINK23,
    LIGHT_LINK34,
    // CV input LEDs (green+red = 2 IDs each)
    LIGHT_CV_A1_GREEN,
    LIGHT_CV_A1_RED,
    LIGHT_CV_A2_GREEN,
    LIGHT_CV_A2_RED,
    LIGHT_CV_A3_GREEN,
    LIGHT_CV_A3_RED,
    LIGHT_CV_B1_GREEN,
    LIGHT_CV_B1_RED,
    LIGHT_CV_B2_GREEN,
    LIGHT_CV_B2_RED,
    LIGHT_CV_B3_GREEN,
    LIGHT_CV_B3_RED,
    LIGHT_CV_C1_GREEN,
    LIGHT_CV_C1_RED,
    LIGHT_CV_C2_GREEN,
    LIGHT_CV_C2_RED,
    LIGHT_CV_C3_GREEN,
    LIGHT_CV_C3_RED,
    LIGHT_CV_D1_GREEN,
    LIGHT_CV_D1_RED,
    LIGHT_CV_D2_GREEN,
    LIGHT_CV_D2_RED,
    LIGHT_CV_D3_GREEN,
    LIGHT_CV_D3_RED,
    NUM_LIGHTS
  };

  // Audio frame buffers
  float inFrame[MAX_AUDIO_FRAME_LENGTH * NUM_INPUT_CHANNELS];
  float outFrame[MAX_AUDIO_FRAME_LENGTH * NUM_OUTPUT_CHANNELS];
  int framePos = 0;
  int outputReadPos = 0;

  // Map from VCV input port index to ER-301 channel index in the interleaved frame
  int inputChannelMap[NUM_INPUTS];

  // Engine state
  std::atomic<bool> engineReady{false};
  std::atomic<bool> audioReady{false};
  std::thread luaThread;
  bool initialized = false;

  // Persistent path strings (Config_init stores raw pointers)
  std::string xRootStr;
  std::string rearRootStr;
  std::string frontRootStr;
  std::string firmwareCfgStr;

  ER301Module()
  {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

    configInput(INPUT_IN1_PORT, "IN1 Audio");
    configInput(INPUT_IN2_PORT, "IN2 Audio");
    configInput(INPUT_IN3_PORT, "IN3 Audio");
    configInput(INPUT_IN4_PORT, "IN4 Audio");
    configInput(INPUT_A1_PORT, "A1 CV");
    configInput(INPUT_B1_PORT, "B1 CV");
    configInput(INPUT_C1_PORT, "C1 CV");
    configInput(INPUT_D1_PORT, "D1 CV");
    configInput(INPUT_A2_PORT, "A2 CV");
    configInput(INPUT_B2_PORT, "B2 CV");
    configInput(INPUT_C2_PORT, "C2 CV");
    configInput(INPUT_D2_PORT, "D2 CV");
    configInput(INPUT_A3_PORT, "A3 CV");
    configInput(INPUT_B3_PORT, "B3 CV");
    configInput(INPUT_C3_PORT, "C3 CV");
    configInput(INPUT_D3_PORT, "D3 CV");
    configInput(INPUT_G1_PORT, "G1 Gate");
    configInput(INPUT_G2_PORT, "G2 Gate");
    configInput(INPUT_G3_PORT, "G3 Gate");
    configInput(INPUT_G4_PORT, "G4 Gate");

    configOutput(OUTPUT_OUT1, "OUT1");
    configOutput(OUTPUT_OUT2, "OUT2");
    configOutput(OUTPUT_OUT3, "OUT3");
    configOutput(OUTPUT_OUT4, "OUT4");

    // Map VCV port indices -> ER-301 interleaved channel indices
    inputChannelMap[INPUT_IN1_PORT] = INPUT_IN1;
    inputChannelMap[INPUT_IN2_PORT] = INPUT_IN2;
    inputChannelMap[INPUT_IN3_PORT] = INPUT_IN3;
    inputChannelMap[INPUT_IN4_PORT] = INPUT_IN4;
    inputChannelMap[INPUT_A1_PORT] = INPUT_A1;
    inputChannelMap[INPUT_B1_PORT] = INPUT_B1;
    inputChannelMap[INPUT_C1_PORT] = INPUT_C1;
    inputChannelMap[INPUT_D1_PORT] = INPUT_D1;
    inputChannelMap[INPUT_A2_PORT] = INPUT_A2;
    inputChannelMap[INPUT_B2_PORT] = INPUT_B2;
    inputChannelMap[INPUT_C2_PORT] = INPUT_C2;
    inputChannelMap[INPUT_D2_PORT] = INPUT_D2;
    inputChannelMap[INPUT_A3_PORT] = INPUT_A3;
    inputChannelMap[INPUT_B3_PORT] = INPUT_B3;
    inputChannelMap[INPUT_C3_PORT] = INPUT_C3;
    inputChannelMap[INPUT_D3_PORT] = INPUT_D3;
    inputChannelMap[INPUT_G1_PORT] = INPUT_G1;
    inputChannelMap[INPUT_G2_PORT] = INPUT_G2;
    inputChannelMap[INPUT_G3_PORT] = INPUT_G3;
    inputChannelMap[INPUT_G4_PORT] = INPUT_G4;

    memset(inFrame, 0, sizeof(inFrame));
    memset(outFrame, 0, sizeof(outFrame));
  }

  ~ER301Module()
  {
    if (initialized)
    {
      Events_push(EVENT_QUIT);
      if (luaThread.joinable())
      {
        luaThread.join();
      }
      Audio_stop();
    }
  }

  void initEngine()
  {
    if (initialized)
      return;
    initialized = true;

    TLS_setName("main");

    // Create directories — store as member variables so pointers stay valid
    std::string homeDir = getenv("HOME");
    rearRootStr = homeDir + "/.od/rear";
    frontRootStr = homeDir + "/.od/front";

    // Find xroot — bundled with plugin or from er-301 source
    std::string pluginPath = rack::asset::plugin(pluginInstance, "res/xroot");
    if (rack::system::isDirectory(pluginPath))
    {
      xRootStr = pluginPath;
    }
    else
    {
      // Fallback: use er-301 source tree xroot
      xRootStr = rack::asset::plugin(pluginInstance, "er-301/xroot");
    }

    // Ensure directories exist
    rack::system::createDirectories(rearRootStr);
    rack::system::createDirectories(frontRootStr);

    Heap_init();
    Timing_init();
    Uart_init();
    Card_init();
    Uart_enable();
    Log_init();

    logInfo("VCV: xRoot = %s", xRootStr.c_str());
    logInfo("VCV: rearRoot = %s", rearRootStr.c_str());
    logInfo("VCV: frontRoot = %s", frontRootStr.c_str());

    firmwareCfgStr = rearRootStr + "/firmware.cfg";
    Config_init(firmwareCfgStr.c_str(), xRootStr.c_str(), rearRootStr.c_str(), frontRootStr.c_str());

    Pump_init();
    Rng_init();
    Gpio_init();
    Events_init();
    USB_init();
    Encoder_init();
    Pwm_init();
    Adc_init();
    Modulation_init();
    Audio_init();
    Display_init();
    od::Random::init();
    od::AudioThread::init();
    audioReady.store(true, std::memory_order_release);

    Events_push(EVENT_DISPLAY_READY);

    logInfo("VCV: Starting Lua interpreter thread...");

    // Start the Lua interpreter thread
    luaThread = std::thread([this]()
                            {
      TLS_setName("lua");
      logInfo("VCV: Lua thread started");
      od::AppInterpreter interp;
      interp.init();
      logInfo("VCV: AppInterpreter initialized");
      interp.execute("package.path = '%s/?.lua;%s/?/init.lua'",
                     globalConfig.xRoot, globalConfig.xRoot);
      interp.execute("app.EMULATION = true");
      interp.execute("app.roots = {x='%s',rear='%s',front='%s'}",
                     globalConfig.xRoot, globalConfig.rearRoot, globalConfig.frontRoot);
      logInfo("VCV: Running logging.lua...");
      interp.execute("dofile('%s/boot/logging.lua')", globalConfig.xRoot);
      logInfo("VCV: Running start.lua...");
      interp.execute("dofile('%s/boot/start.lua')", globalConfig.xRoot);
      logInfo("VCV: start.lua finished");
      engineReady.store(true, std::memory_order_release); });

    Audio_start();
  }

  void process(const ProcessArgs &args) override
  {
    if (!initialized)
    {
      initEngine();
    }

    // Write one sample of input into the interleaved input frame
    int offset = framePos * NUM_INPUT_CHANNELS;
    for (int i = 0; i < NUM_INPUTS; i++)
    {
      float voltage = inputs[i].getVoltage();
      // ER-301 internal range is roughly -1.0 to 1.0 for audio
      // VCV is -5V to 5V for audio, -10V to 10V for CV
      // Scale: divide by FULLSCALE_IN_VOLTS (10.0)
      inFrame[offset + inputChannelMap[i]] = voltage / FULLSCALE_IN_VOLTS;
    }

    // Read one sample from the output buffer
    if (outputReadPos < FRAMELENGTH)
    {
      int outOffset = outputReadPos * NUM_OUTPUT_CHANNELS;
      for (int i = 0; i < NUM_OUTPUTS; i++)
      {
        // Scale back to VCV voltage range
        float sample = outFrame[outOffset + i];
        outputs[i].setVoltage(sample * FULLSCALE_IN_VOLTS);
      }
      outputReadPos++;
    }

    framePos++;

    // When we've accumulated a full frame, process it
    if (framePos >= FRAMELENGTH)
    {
      memset(outFrame, 0, sizeof(float) * NUM_OUTPUT_CHANNELS * FRAMELENGTH);

      // Only call the DSP engine after AudioThread is initialized
      if (audioReady.load(std::memory_order_acquire))
      {
        Pump_callback(inFrame, outFrame);
        Pump_resetThrottle();
      }

      framePos = 0;
      outputReadPos = 0;
    }

    // Update GPIO-driven LEDs
    lights[LIGHT_DIAL1].setBrightness(Gpio_read(LED_DIAL1) ? 1.f : 0.f);
    lights[LIGHT_DIAL2].setBrightness(Gpio_read(LED_DIAL2) ? 1.f : 0.f);
    lights[LIGHT_IO].setBrightness(Gpio_read(LED_IO) ? 1.f : 0.f);
    lights[LIGHT_SAFE].setBrightness(Gpio_read(LED_SAFE) ? 1.f : 0.f);
    lights[LIGHT_OUT1].setBrightness(Gpio_read(LED_OUT1) ? 1.f : 0.f);
    lights[LIGHT_OUT2].setBrightness(Gpio_read(LED_OUT2) ? 1.f : 0.f);
    lights[LIGHT_OUT3].setBrightness(Gpio_read(LED_OUT3) ? 1.f : 0.f);
    lights[LIGHT_OUT4].setBrightness(Gpio_read(LED_OUT4) ? 1.f : 0.f);
    lights[LIGHT_LINK12].setBrightness(Gpio_read(LED_LINK12) ? 1.f : 0.f);
    lights[LIGHT_LINK23].setBrightness(Gpio_read(LED_LINK23) ? 1.f : 0.f);
    lights[LIGHT_LINK34].setBrightness(Gpio_read(LED_LINK34) ? 1.f : 0.f);

    // Update PWM-driven CV input LEDs (green/red per channel)
    float pwmR, pwmG;
    static const int cvLightIds[] = {
        LIGHT_CV_A1_GREEN, LIGHT_CV_A2_GREEN, LIGHT_CV_A3_GREEN,
        LIGHT_CV_B1_GREEN, LIGHT_CV_B2_GREEN, LIGHT_CV_B3_GREEN,
        LIGHT_CV_C1_GREEN, LIGHT_CV_C2_GREEN, LIGHT_CV_C3_GREEN,
        LIGHT_CV_D1_GREEN, LIGHT_CV_D2_GREEN, LIGHT_CV_D3_GREEN};
    for (int i = 0; i < 12; i++)
    {
      Pwm_getLed(i, &pwmR, &pwmG);
      lights[cvLightIds[i]].setBrightness(pwmG);
      lights[cvLightIds[i] + 1].setBrightness(pwmR);
    }
  }
};

// Forward declarations for encoder HAL
void VCV_addEncoderDelta(int delta);

// ─── Clickable button overlay widget ───
struct ER301Button : OpaqueWidget
{
  uint32_t gpioId;
  bool isBlue;
  std::string topLabel;  // label above button (M1, CANCEL, etc.)
  std::string botLabel;  // label below top label (QUICKSAVE, etc.)
  bool pressed = false;

  ER301Button(uint32_t id, Vec pos, Vec size, bool blue, const char *top = "", const char *bot = "")
      : gpioId(id), isBlue(blue), topLabel(top), botLabel(bot)
  {
    box.pos = pos;
    box.size = size;
  }

  void onButton(const ButtonEvent &e) override
  {
    if (e.button == GLFW_MOUSE_BUTTON_LEFT)
    {
      if (e.action == GLFW_PRESS)
      {
        pressed = true;
        Gpio_write(gpioId, false);
        e.consume(this);
      }
      else if (e.action == GLFW_RELEASE)
      {
        pressed = false;
        Gpio_write(gpioId, true);
        e.consume(this);
      }
    }
  }

  void onDragEnd(const DragEndEvent &e) override
  {
    if (pressed)
    {
      pressed = false;
      Gpio_write(gpioId, true);
    }
  }

  void draw(const DrawArgs &args) override
  {
    NVGcontext *vg = args.vg;
    float w = box.size.x;
    float h = box.size.y;
    float r = 2.0f;

    // Dark, minimal button style for VCV Rack aesthetic
    NVGcolor face;
    if (isBlue)
      face = pressed ? nvgRGB(35, 65, 140) : nvgRGB(50, 90, 180);
    else
      face = pressed ? nvgRGB(55, 55, 55) : nvgRGB(75, 75, 75);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, w, h, r);
    nvgFillColor(vg, face);
    nvgFill(vg);

    // Subtle top highlight
    if (!pressed)
    {
      nvgBeginPath(vg);
      nvgRoundedRect(vg, 0.5f, 0.5f, w - 1, 1, r);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, isBlue ? 40 : 30));
      nvgFill(vg);
    }

    // Border
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0, 0, w, h, r);
    nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 80));
    nvgStrokeWidth(vg, 0.75f);
    nvgStroke(vg);

    // Top label above button
    if (!topLabel.empty())
    {
      nvgFontSize(vg, 8);
      nvgFillColor(vg, nvgRGB(40, 40, 40));
      nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
      nvgText(vg, w / 2, -2, topLabel.c_str(), NULL);
    }
    if (!botLabel.empty())
    {
      nvgFontSize(vg, 6.5f);
      nvgFillColor(vg, nvgRGB(80, 80, 80));
      nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
      nvgText(vg, w / 2, -10, botLabel.c_str(), NULL);
    }
  }
};

// ─── Toggle switch widget (3-position metal toggle) ───
struct ER301Toggle : OpaqueWidget
{
  uint32_t idA, idB;
  std::string label;
  const char *labels[3]; // up, mid, down

  ER301Toggle(uint32_t a, uint32_t b, Vec pos, Vec size,
              const char *lbl, const char *up, const char *mid, const char *dn)
      : idA(a), idB(b), label(lbl)
  {
    box.pos = pos;
    box.size = size;
    labels[0] = up;
    labels[1] = mid;
    labels[2] = dn;
  }

  int getState()
  {
    if (Gpio_read(idA))
      return 0;
    if (Gpio_read(idB))
      return 2;
    return 1;
  }

  void setState(int s)
  {
    switch (s)
    {
    case 0:
      Gpio_write(idA, true);
      Gpio_write(idB, false);
      break;
    case 1:
      Gpio_write(idA, false);
      Gpio_write(idB, false);
      break;
    case 2:
      Gpio_write(idA, false);
      Gpio_write(idB, true);
      break;
    }
  }

  void onButton(const ButtonEvent &e) override
  {
    if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS)
    {
      float p = e.pos.y / box.size.y;
      int target = (p < 0.333f) ? 0 : (p < 0.667f) ? 1
                                                     : 2;
      setState(target);
      e.consume(this);
    }
  }

  void draw(const DrawArgs &args) override
  {
    NVGcontext *vg = args.vg;
    int state = getState();

    // Title above (like real panel: "STORAGE", "MODE")
    if (!label.empty())
    {
      nvgFontSize(vg, 8);
      nvgFillColor(vg, nvgRGB(40, 40, 40));
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
      nvgText(vg, 0, -3, label.c_str(), NULL);
    }

    // Toggle body - vertical metal toggle like 4ms/standard VCV
    float cx = 7;
    float toggleH = box.size.y - 8;
    float toggleTop = 4;

    // Base plate
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx - 5, toggleTop, 10, toggleH, 2);
    nvgFillColor(vg, nvgRGB(30, 30, 30));
    nvgFill(vg);

    // Lever
    float leverLen = toggleH * 0.45f;
    float leverCy;
    if (state == 0)
      leverCy = toggleTop + toggleH * 0.22f;
    else if (state == 2)
      leverCy = toggleTop + toggleH * 0.78f;
    else
      leverCy = toggleTop + toggleH * 0.5f;

    float leverTop = leverCy - leverLen / 2;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, cx - 3, leverTop, 6, leverLen, 2);
    NVGpaint metalGrad = nvgLinearGradient(vg, cx - 3, leverTop, cx + 3, leverTop + leverLen,
                                           nvgRGB(210, 210, 212), nvgRGB(160, 160, 162));
    nvgFillPaint(vg, metalGrad);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGB(100, 100, 100));
    nvgStrokeWidth(vg, 0.5f);
    nvgStroke(vg);

    // Position labels with arrows
    float textX = 17;
    float positions[] = {toggleTop + toggleH * 0.22f, toggleTop + toggleH * 0.5f, toggleTop + toggleH * 0.78f};
    for (int i = 0; i < 3; i++)
    {
      nvgFontSize(vg, 7);
      nvgFillColor(vg, nvgRGB(50, 50, 50));
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgText(vg, textX, positions[i], labels[i], NULL);
    }
  }
};

// ─── Encoder knob widget ───
struct ER301Knob : OpaqueWidget
{
  float dragY = 0;
  bool dragging = false;

  ER301Knob(Vec pos, Vec size)
  {
    box.pos = pos;
    box.size = size;
  }

  void onButton(const ButtonEvent &e) override
  {
    if (e.button == GLFW_MOUSE_BUTTON_LEFT)
    {
      if (e.action == GLFW_PRESS)
      {
        dragging = true;
        dragY = APP->scene->rack->getMousePos().y;
        e.consume(this);
      }
      else if (e.action == GLFW_RELEASE)
      {
        dragging = false;
        e.consume(this);
      }
    }
  }

  void onDragMove(const DragMoveEvent &e) override
  {
    float delta = -e.mouseDelta.y * 0.5f;
    VCV_addEncoderDelta((int)(delta * 5));
  }

  void onHover(const HoverEvent &e) override
  {
    e.consume(this);
  }

  void onHoverScroll(const HoverScrollEvent &e) override
  {
    float delta = e.scrollDelta.y;
    VCV_addEncoderDelta((int)(delta * 0.5f));
    e.consume(this);
  }

  void draw(const DrawArgs &args) override
  {
    NVGcontext *vg = args.vg;
    float cx = box.size.x / 2;
    float cy = box.size.y / 2;
    float r = std::min(cx, cy) - 4;

    // Shadow
    nvgBeginPath(vg);
    nvgCircle(vg, cx + 2, cy + 2, r + 1);
    nvgFillColor(vg, nvgRGBA(0, 0, 0, 50));
    nvgFill(vg);

    // Knob body - dark with subtle gradient
    NVGpaint knobGrad = nvgRadialGradient(vg, cx - r * 0.3f, cy - r * 0.3f, 0, r * 2,
                                          nvgRGB(50, 50, 50), nvgRGB(15, 15, 15));
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgFillPaint(vg, knobGrad);
    nvgFill(vg);

    // Outer ring
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r);
    nvgStrokeColor(vg, nvgRGB(5, 5, 5));
    nvgStrokeWidth(vg, 2.0f);
    nvgStroke(vg);

    // Grip notches (like rubber ridges on real knob)
    for (int i = 0; i < 20; i++)
    {
      float angle = i * 18.0f * (float)M_PI / 180.0f;
      float x1 = cx + (r - 4) * cosf(angle);
      float y1 = cy + (r - 4) * sinf(angle);
      float x2 = cx + (r - 1) * cosf(angle);
      float y2 = cy + (r - 1) * sinf(angle);
      nvgBeginPath(vg);
      nvgMoveTo(vg, x1, y1);
      nvgLineTo(vg, x2, y2);
      nvgStrokeColor(vg, nvgRGB(40, 40, 40));
      nvgStrokeWidth(vg, 2.0f);
      nvgStroke(vg);
    }

    // Center cap
    nvgBeginPath(vg);
    nvgCircle(vg, cx, cy, r * 0.35f);
    nvgFillColor(vg, nvgRGB(25, 25, 25));
    nvgFill(vg);
  }
};

struct ER301Widget : ModuleWidget
{
  // NanoVG image handles for display textures
  int mainImage = -1;
  int subImage = -1;
  uint8_t mainPixels[MAIN_HORIZONTAL_PIXELS * MAIN_VERTICAL_PIXELS * 4];
  uint8_t subPixels[SUB_HORIZONTAL_PIXELS * SUB_VERTICAL_PIXELS * 4];

  static constexpr int SCREEN_BRIGHTNESS = 15;
  static constexpr float SCREEN_TINT = 0.85f;

  // 1:1 with emulator layout (505x370), which matches real ER-301 proportions
  static constexpr float S = 1.0f;
  static constexpr float MARGIN = 16;

  // Emu layout constants (direct from constants.h)
  static constexpr float MAIN_DW = 256;
  static constexpr float MAIN_DH = 64;
  static constexpr float B_SP = 6;
  static constexpr float DB_SP = 24;
  static constexpr float BTN_W = 256 / 6 - 6; // 36
  static constexpr float BTN_H = BTN_W;
  static constexpr float KNOB_W_S = 256 / 2 - 6; // 122
  static constexpr float KNOB_H_S = 100;
  static constexpr float SUB_DW = 128;
  static constexpr float SUB_DH = 64;
  static constexpr float J_OUTER = 17;
  static constexpr float J_DIVIDER = 50;
  static constexpr float J_HSPACING = 2 * (17 + 8); // 50
  static constexpr float TOGGLE_WS = 256 / 6 - 6 + 6; // 42
  static constexpr float TOGGLE_HS = BTN_H;

  // Computed positions
  float mainDispX, mainDispY;
  float mb1X, mb1Y, mb2X, mb3X, mb4X, mb5X, mb6X;
  float knobX, knobY;
  float subDispX, subDispY;
  float sb1Y, hb1Y;
  float j1X, j2X, j3X, j4X;
  float j1Y, j2Y, j3Y, j4Y, j5Y, j6Y, j7Y;
  float jb1X, jb1Y, jb2Y, jb3Y, jb4Y;
  float tStorageX, tStorageY, tModeX, tModeY;
  float ledOut1X;

  ER301Widget(ER301Module *module)
  {
    setModule(module);

    memset(mainPixels, 0, sizeof(mainPixels));
    memset(subPixels, 0, sizeof(subPixels));

    // Compute all positions from emu constants
    mainDispX = MARGIN;
    mainDispY = MARGIN;

    mb1X = mainDispX + B_SP / 2 + 2 * S;
    mb1Y = mainDispY + MAIN_DH + DB_SP;
    mb2X = mb1X + BTN_W + B_SP;
    mb3X = mb2X + BTN_W + B_SP;
    mb4X = mb3X + BTN_W + B_SP;
    mb5X = mb4X + BTN_W + B_SP;
    mb6X = mb5X + BTN_W + B_SP;

    knobX = mainDispX;
    knobY = mb1Y + BTN_H + B_SP;

    subDispX = mainDispX + MAIN_DW / 2;
    subDispY = knobY + KNOB_H_S - SUB_DH - 12 * S;

    sb1Y = subDispY + SUB_DH + DB_SP;
    hb1Y = sb1Y + BTN_H + DB_SP;

    // Jack positions (right panel)
    j7Y = hb1Y + BTN_H / 2;
    j1Y = mainDispY + J_OUTER;
    float jvSpacing = (j7Y - j1Y) / 6.0f;
    j2Y = j1Y + jvSpacing;
    j3Y = j2Y + jvSpacing;
    j4Y = j3Y + jvSpacing;
    j5Y = j4Y + jvSpacing;
    j6Y = j5Y + jvSpacing;

    // Jack columns (exact emulator layout)
    j1X = mainDispX + MAIN_DW + J_DIVIDER;
    j2X = j1X + J_HSPACING;
    j3X = j2X + J_HSPACING;
    j4X = j3X + J_HSPACING;

    jb1X = j1X - BTN_W / 2;
    jb1Y = j1Y - BTN_H / 2;
    jb2Y = j2Y - BTN_H / 2;
    jb3Y = j3Y - BTN_H / 2;
    jb4Y = j4Y - BTN_H / 2;

    tStorageX = mb1X - 5;
    tStorageY = j7Y - TOGGLE_HS / 2;
    tModeX = mb3X - 5;
    tModeY = tStorageY;

    ledOut1X = (mainDispX + MAIN_DW + jb1X) / 2;

    // Panel size from emulator layout (505x370 -> round to HP grid)
    float panelW = j4X + J_OUTER + MARGIN;
    int hp = (int)ceilf(panelW / RACK_GRID_WIDTH);
    panelW = hp * RACK_GRID_WIDTH;
    box.size = Vec(panelW, RACK_GRID_HEIGHT);

    // === Standard VCV screws ===
    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(panelW - 2 * RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewSilver>(Vec(panelW - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // === M1-M6 main buttons (gray, hardware labels) ===
    addChild(new ER301Button(BUTTON_MAIN1, Vec(mb1X, mb1Y), Vec(BTN_W, BTN_H), false, "M1", "QUICKSAVE"));
    addChild(new ER301Button(BUTTON_MAIN2, Vec(mb2X, mb1Y), Vec(BTN_W, BTN_H), false, "M2"));
    addChild(new ER301Button(BUTTON_MAIN3, Vec(mb3X, mb1Y), Vec(BTN_W, BTN_H), false, "M3"));
    addChild(new ER301Button(BUTTON_MAIN4, Vec(mb4X, mb1Y), Vec(BTN_W, BTN_H), false, "M4"));
    addChild(new ER301Button(BUTTON_MAIN5, Vec(mb5X, mb1Y), Vec(BTN_W, BTN_H), false, "M5"));
    addChild(new ER301Button(BUTTON_MAIN6, Vec(mb6X, mb1Y), Vec(BTN_W, BTN_H), false, "M6", "FOCUS"));

    // === Dial buttons (blue) and Sub buttons (gray) ===
    addChild(new ER301Button(BUTTON_DIAL1, Vec(mb1X, sb1Y), Vec(BTN_W, BTN_H), true));
    addChild(new ER301Button(BUTTON_DIAL2, Vec(mb2X, sb1Y), Vec(BTN_W, BTN_H), true, "CANCEL"));
    addChild(new ER301Button(BUTTON_DIAL3, Vec(mb3X, sb1Y), Vec(BTN_W, BTN_H), true, "ZERO"));
    addChild(new ER301Button(BUTTON_SUB1, Vec(mb4X, sb1Y), Vec(BTN_W, BTN_H), false, "S1"));
    addChild(new ER301Button(BUTTON_SUB2, Vec(mb5X, sb1Y), Vec(BTN_W, BTN_H), false, "S2"));
    addChild(new ER301Button(BUTTON_SUB3, Vec(mb6X, sb1Y), Vec(BTN_W, BTN_H), false, "S3", "FOCUS"));

    // === Hard buttons (blue: ENTER/UP/SHIFT) ===
    addChild(new ER301Button(BUTTON_ENTER, Vec(mb4X, hb1Y), Vec(BTN_W, BTN_H), true, "ENTER", "COMMIT"));
    addChild(new ER301Button(BUTTON_UP, Vec(mb5X, hb1Y), Vec(BTN_W, BTN_H), true, "UP"));
    addChild(new ER301Button(BUTTON_SHIFT, Vec(mb6X, hb1Y), Vec(BTN_W, BTN_H), true, "SHIFT"));

    // === Channel select buttons 1-4 (smaller gray) ===
    addChild(new ER301Button(BUTTON_SELECT1, Vec(jb1X, jb1Y), Vec(BTN_W, BTN_H), false, "", ""));
    addChild(new ER301Button(BUTTON_SELECT2, Vec(jb1X, jb2Y), Vec(BTN_W, BTN_H), false, "", ""));
    addChild(new ER301Button(BUTTON_SELECT3, Vec(jb1X, jb3Y), Vec(BTN_W, BTN_H), false, "", ""));
    addChild(new ER301Button(BUTTON_SELECT4, Vec(jb1X, jb4Y), Vec(BTN_W, BTN_H), false, "", ""));

    // === Encoder knob ===
    addChild(new ER301Knob(Vec(knobX, knobY), Vec(KNOB_W_S, KNOB_H_S)));

    // === Toggle switches ===
    addChild(new ER301Toggle(TOGGLE_STORAGE_A, TOGGLE_STORAGE_B,
                             Vec(tStorageX, tStorageY), Vec(TOGGLE_WS, TOGGLE_HS),
                             "STORAGE", "user", "admin", "eject"));
    addChild(new ER301Toggle(TOGGLE_MODE_A, TOGGLE_MODE_B,
                             Vec(tModeX, tModeY), Vec(TOGGLE_WS, TOGGLE_HS),
                             "MODE", "hold", "edit", "scope"));

    // === GPIO LEDs (VCV standard lights, positions from real panel) ===
    // Fine/Coarse LEDs near knob (left side, below sub display)
    float ledDial1X = mb1X + 10;
    float ledDial1Y = subDispY + SUB_DH - 12;
    float ledDial2X = ledDial1X + BTN_W / 2;
    float ledDial2Y = ledDial1Y + BTN_H / 2;
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledDial1X, ledDial1Y), module, ER301Module::LIGHT_DIAL1));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledDial2X, ledDial2Y), module, ER301Module::LIGHT_DIAL2));

    // I/O and Safe LEDs (near storage/mode toggle area)
    float ledIOX = mb2X + 4;
    float ledIOY = j7Y - BTN_W / 4;
    float ledSafeY = j7Y + BTN_W / 4;
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledIOX, ledIOY), module, ER301Module::LIGHT_IO));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledIOX, ledSafeY), module, ER301Module::LIGHT_SAFE));

    // Output LEDs (orange/yellow, right of select buttons on real panel)
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(ledOut1X, j1Y), module, ER301Module::LIGHT_OUT1));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(ledOut1X, j2Y), module, ER301Module::LIGHT_OUT2));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(ledOut1X, j3Y), module, ER301Module::LIGHT_OUT3));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(ledOut1X, j4Y), module, ER301Module::LIGHT_OUT4));

    // Link LEDs (red, between output rows)
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledOut1X, (j1Y + j2Y) / 2), module, ER301Module::LIGHT_LINK12));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledOut1X, (j2Y + j3Y) / 2), module, ER301Module::LIGHT_LINK23));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ledOut1X, (j3Y + j4Y) / 2), module, ER301Module::LIGHT_LINK34));

    // === CV input LEDs (green/red bicolor, above each A1-D3 jack) ===
    static const int cvGreenIds[] = {
        ER301Module::LIGHT_CV_A1_GREEN, ER301Module::LIGHT_CV_A2_GREEN, ER301Module::LIGHT_CV_A3_GREEN,
        ER301Module::LIGHT_CV_B1_GREEN, ER301Module::LIGHT_CV_B2_GREEN, ER301Module::LIGHT_CV_B3_GREEN,
        ER301Module::LIGHT_CV_C1_GREEN, ER301Module::LIGHT_CV_C2_GREEN, ER301Module::LIGHT_CV_C3_GREEN,
        ER301Module::LIGHT_CV_D1_GREEN, ER301Module::LIGHT_CV_D2_GREEN, ER301Module::LIGHT_CV_D3_GREEN};
    float cvJxArr[] = {j1X, j2X, j3X, j4X};
    float cvJyArr[] = {j5Y, j6Y, j7Y};
    for (int col = 0; col < 4; col++)
    {
      for (int row = 0; row < 3; row++)
      {
        int idx = col * 3 + row;
        // Place LED to the left of the jack with breathing room
        addChild(createLightCentered<MediumLight<GreenRedLight>>(
            Vec(cvJxArr[col] - 19, cvJyArr[row]), module, cvGreenIds[idx]));
      }
    }

    // === Jack ports ===
    // Gate inputs: G1-G4 at j2X, j1Y-j4Y
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j1Y), module, ER301Module::INPUT_G1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j2Y), module, ER301Module::INPUT_G2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j3Y), module, ER301Module::INPUT_G3_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j4Y), module, ER301Module::INPUT_G4_PORT));

    // Audio inputs: IN1-IN4 at j3X, j1Y-j4Y
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j1Y), module, ER301Module::INPUT_IN1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j2Y), module, ER301Module::INPUT_IN2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j3Y), module, ER301Module::INPUT_IN3_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j4Y), module, ER301Module::INPUT_IN4_PORT));

    // Outputs: OUT1-OUT4 at j4X, j1Y-j4Y
    addOutput(createOutputCentered<PJ301MPort>(Vec(j4X, j1Y), module, ER301Module::OUTPUT_OUT1));
    addOutput(createOutputCentered<PJ301MPort>(Vec(j4X, j2Y), module, ER301Module::OUTPUT_OUT2));
    addOutput(createOutputCentered<PJ301MPort>(Vec(j4X, j3Y), module, ER301Module::OUTPUT_OUT3));
    addOutput(createOutputCentered<PJ301MPort>(Vec(j4X, j4Y), module, ER301Module::OUTPUT_OUT4));

    // CV inputs: A1-A3, B1-B3, C1-C3, D1-D3 in bottom jack section
    addInput(createInputCentered<PJ301MPort>(Vec(j1X, j5Y), module, ER301Module::INPUT_A1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j1X, j6Y), module, ER301Module::INPUT_A2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j1X, j7Y), module, ER301Module::INPUT_A3_PORT));

    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j5Y), module, ER301Module::INPUT_B1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j6Y), module, ER301Module::INPUT_B2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j2X, j7Y), module, ER301Module::INPUT_B3_PORT));

    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j5Y), module, ER301Module::INPUT_C1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j6Y), module, ER301Module::INPUT_C2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j3X, j7Y), module, ER301Module::INPUT_C3_PORT));

    addInput(createInputCentered<PJ301MPort>(Vec(j4X, j5Y), module, ER301Module::INPUT_D1_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j4X, j6Y), module, ER301Module::INPUT_D2_PORT));
    addInput(createInputCentered<PJ301MPort>(Vec(j4X, j7Y), module, ER301Module::INPUT_D3_PORT));
  }

  ~ER301Widget()
  {
  }

  void decodeMainDisplay(DisplayBuffer *buf)
  {
    uint16_t *src = (uint16_t *)buf->main;
    for (int y = 0; y < MAIN_VERTICAL_PIXELS; y++)
    {
      int yy = MAIN_VERTICAL_PIXELS - y - 1;
      for (int x = 0; x < MAIN_HORIZONTAL_PIXELS; x++)
      {
        int xx = MAIN_HORIZONTAL_PIXELS - x - 1;
        uint16_t cell = src[(yy << 7) + (xx >> 1)];
        int shift = ((~xx & 1) << 2);
        int value = (cell >> shift) & 0xF;
        value *= SCREEN_BRIGHTNESS;
        int idx = (y * MAIN_HORIZONTAL_PIXELS + x) * 4;
        mainPixels[idx + 0] = (uint8_t)std::min(value, 255);
        mainPixels[idx + 1] = (uint8_t)std::min((int)(value * SCREEN_TINT), 255);
        mainPixels[idx + 2] = 0;
        mainPixels[idx + 3] = 255;
      }
    }
  }

  void decodeSubDisplay(DisplayBuffer *buf)
  {
    uint16_t *src = (uint16_t *)buf->sub;
    for (int y = 0; y < SUB_VERTICAL_PIXELS; y++)
    {
      int yy = SUB_VERTICAL_PIXELS - y - 1;
      int shift = yy & 0b111;
      for (int x = 0; x < SUB_HORIZONTAL_PIXELS; x++)
      {
        int xx = SUB_HORIZONTAL_PIXELS - x - 1;
        uint16_t cell = src[((yy >> 3) << 7) + xx];
        int value = (cell >> shift) & 1;
        value *= 0xF * SCREEN_BRIGHTNESS;
        int idx = (y * SUB_HORIZONTAL_PIXELS + x) * 4;
        subPixels[idx + 0] = (uint8_t)std::min(value, 255);
        subPixels[idx + 1] = (uint8_t)std::min((int)(value * SCREEN_TINT), 255);
        subPixels[idx + 2] = 0;
        subPixels[idx + 3] = 255;
      }
    }
  }

  void draw(const DrawArgs &args) override
  {
    NVGcontext *vg = args.vg;

    // Signal the engine to prepare the next display frame
    ER301Module *mod = dynamic_cast<ER301Module *>(module);
    if (mod && mod->audioReady.load(std::memory_order_acquire))
    {
      Events_push(EVENT_DISPLAY_READY);
    }

    float pw = box.size.x;
    float ph = box.size.y;

    // ── Panel background (light silver matching Teletype/monome style) ──
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, pw, ph);
    nvgFillColor(vg, nvgRGB(228, 228, 230));
    nvgFill(vg);

    // ── Panel border ──
    nvgBeginPath(vg);
    nvgRect(vg, 0.5f, 0.5f, pw - 1, ph - 1);
    nvgStrokeColor(vg, nvgRGB(160, 160, 162));
    nvgStrokeWidth(vg, 0.75f);
    nvgStroke(vg);

    // ── Title: "ER-301  SOUND COMPUTER" centered at top ──
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    float titleY = 8;
    nvgFontSize(vg, 13);
    nvgFillColor(vg, nvgRGB(50, 50, 50));
    nvgText(vg, pw * 0.38f, titleY, "ER-301", NULL);
    nvgFontSize(vg, 9);
    nvgFillColor(vg, nvgRGB(80, 80, 80));
    nvgText(vg, pw * 0.60f, titleY, "SOUND COMPUTER", NULL);

    // ── Jack column headers: G, IN, OUT ──
    nvgFontSize(vg, 9);
    nvgFillColor(vg, nvgRGB(50, 50, 50));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgText(vg, j2X, j1Y - J_OUTER + 4, "G", NULL);
    nvgText(vg, j3X, j1Y - J_OUTER + 4, "IN", NULL);
    nvgText(vg, j4X, j1Y - J_OUTER + 4, "OUT", NULL);

    // ── Vertical divider lines between jack columns ──
    float jackTop = j1Y - J_OUTER + 8;
    float jackBot = j4Y + J_OUTER - 4;
    nvgStrokeColor(vg, nvgRGB(175, 175, 177));
    nvgStrokeWidth(vg, 0.5f);
    float divX1 = (j1X + j2X) / 2;
    float divX2 = (j2X + j3X) / 2;
    float divX3 = (j3X + j4X) / 2;
    nvgBeginPath(vg);
    nvgMoveTo(vg, divX1, jackTop); nvgLineTo(vg, divX1, jackBot);
    nvgMoveTo(vg, divX2, jackTop); nvgLineTo(vg, divX2, jackBot);
    nvgMoveTo(vg, divX3, jackTop); nvgLineTo(vg, divX3, jackBot);
    nvgStroke(vg);

    // ── Channel numbers on select buttons ──
    nvgFontSize(vg, 11);
    nvgFillColor(vg, nvgRGB(220, 220, 220));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, jb1X + BTN_W / 2, jb1Y + BTN_H / 2, "1", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb2Y + BTN_H / 2, "2", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb3Y + BTN_H / 2, "3", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb4Y + BTN_H / 2, "4", NULL);

    // ── "linked" labels to the right of link LEDs ──
    nvgFontSize(vg, 6.5f);
    nvgFillColor(vg, nvgRGB(60, 60, 60));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    float linkX = ledOut1X + 7;
    nvgText(vg, linkX, (j1Y + j2Y) / 2, "linked", NULL);
    nvgText(vg, linkX, (j2Y + j3Y) / 2, "linked", NULL);
    nvgText(vg, linkX, (j3Y + j4Y) / 2, "linked", NULL);

    // ── Fine/Coarse LED labels near knob ──
    float ledDial1X = mb1X + 10;
    float ledDial1Y = subDispY + SUB_DH - 12;
    float ledDial2X = ledDial1X + BTN_W / 2;
    float ledDial2Y = ledDial1Y + BTN_H / 2;
    nvgFontSize(vg, 6.5f);
    nvgFillColor(vg, nvgRGB(50, 50, 50));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, ledDial1X - 7, ledDial1Y, "fine", NULL);
    nvgText(vg, ledDial2X - 7, ledDial2Y, "coarse", NULL);

    // ── I/O and Safe LED labels ──
    float ledIOX = mb2X + 4;
    float ledIOY = j7Y - BTN_W / 4;
    float ledSafeY = j7Y + BTN_W / 4;
    nvgFontSize(vg, 6.5f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, ledIOX + 7, ledIOY, "I/O", NULL);
    nvgText(vg, ledIOX + 7, ledSafeY, "safe", NULL);

    // ── Storage section divider ──
    float stX = 4;
    float stY = tStorageY - 14;
    float stW = (mb2X + mb3X) / 2 - stX;
    float stH = ph - 18 - stY;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, stX, stY, stW, stH, 2);
    nvgStrokeColor(vg, nvgRGB(175, 175, 177));
    nvgStrokeWidth(vg, 0.5f);
    nvgStroke(vg);

    // ── ABCD jack section divider ──
    float abcdX = j1X - J_HSPACING / 2 - 4;
    float abcdY = (j4Y + j5Y) / 2;
    float abcdW = pw - 4 - abcdX;
    float abcdH = ph - 18 - abcdY;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, abcdX, abcdY, abcdW, abcdH, 2);
    nvgStrokeColor(vg, nvgRGB(175, 175, 177));
    nvgStrokeWidth(vg, 0.5f);
    nvgStroke(vg);

    // ── CV jack labels (A1-D3) ──
    nvgFontSize(vg, 8);
    nvgFillColor(vg, nvgRGB(60, 60, 60));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    const char *cvCols[] = {"A", "B", "C", "D"};
    float jxArr[] = {j1X, j2X, j3X, j4X};
    float jyArr[] = {j5Y, j6Y, j7Y};
    for (int col = 0; col < 4; col++)
    {
      for (int row = 0; row < 3; row++)
      {
        char buf[4];
        snprintf(buf, sizeof(buf), "%s%d", cvCols[col], row + 1);
        nvgText(vg, jxArr[col], jyArr[row] - 14, buf, NULL);
      }
    }

    // ── Bottom voltage specs (matching real panel) ──
    float bottomY = ph - 4;
    nvgFontSize(vg, 6);
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgFillColor(vg, nvgRGB(80, 80, 80));
    nvgText(vg, pw * 0.17f, bottomY, "G: 0V-10V | 12-bit", NULL);
    nvgText(vg, pw * 0.5f, bottomY, "IN+ABCD: -10V-10V | 16-bit", NULL);
    nvgText(vg, pw * 0.83f, bottomY, "OUT: -7V-7V | 24-bit", NULL);

    // ── Main Display ──
    nvgBeginPath(vg);
    nvgRect(vg, mainDispX - 2, mainDispY - 2, MAIN_DW + 4, MAIN_DH + 4);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);

    DisplayBuffer *dispBuf = Display_getLastPutBuffer();
    if (dispBuf)
    {
      decodeMainDisplay(dispBuf);
      if (mainImage < 0)
        mainImage = nvgCreateImageRGBA(vg, MAIN_HORIZONTAL_PIXELS, MAIN_VERTICAL_PIXELS, 0, mainPixels);
      else
        nvgUpdateImage(vg, mainImage, mainPixels);

      if (mainImage >= 0)
      {
        NVGpaint paint = nvgImagePattern(vg, mainDispX, mainDispY, MAIN_DW, MAIN_DH, 0, mainImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, mainDispX, mainDispY, MAIN_DW, MAIN_DH);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
      }
    }
    else
    {
      nvgBeginPath(vg);
      nvgRect(vg, mainDispX, mainDispY, MAIN_DW, MAIN_DH);
      nvgFillColor(vg, nvgRGB(0, 0, 0));
      nvgFill(vg);
    }

    // ── Sub Display ──
    nvgBeginPath(vg);
    nvgRect(vg, subDispX - 2, subDispY - 2, SUB_DW + 4, SUB_DH + 4);
    nvgFillColor(vg, nvgRGB(0, 0, 0));
    nvgFill(vg);

    if (dispBuf)
    {
      decodeSubDisplay(dispBuf);
      if (subImage < 0)
        subImage = nvgCreateImageRGBA(vg, SUB_HORIZONTAL_PIXELS, SUB_VERTICAL_PIXELS, 0, subPixels);
      else
        nvgUpdateImage(vg, subImage, subPixels);

      if (subImage >= 0)
      {
        NVGpaint paint = nvgImagePattern(vg, subDispX, subDispY, SUB_DW, SUB_DH, 0, subImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, subDispX, subDispY, SUB_DW, SUB_DH);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
      }
    }
    else
    {
      nvgBeginPath(vg);
      nvgRect(vg, subDispX, subDispY, SUB_DW, SUB_DH);
      nvgFillColor(vg, nvgRGB(0, 0, 0));
      nvgFill(vg);
    }

    ModuleWidget::draw(args);
  }
};

Model *modelER301 = createModel<ER301Module, ER301Widget>("ER301");
