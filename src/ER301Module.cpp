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
      nvgFontSize(vg, 7);
      nvgFillColor(vg, nvgRGB(50, 50, 50));
      nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
      nvgText(vg, w / 2, -1, topLabel.c_str(), NULL);
    }
    if (!botLabel.empty())
    {
      nvgFontSize(vg, 5.5f);
      nvgFillColor(vg, nvgRGB(100, 100, 100));
      nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
      nvgText(vg, w / 2, -8, botLabel.c_str(), NULL);
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

    // Title above
    if (!label.empty())
    {
      nvgFontSize(vg, 7);
      nvgFillColor(vg, nvgRGB(50, 50, 50));
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
      nvgText(vg, 0, -2, label.c_str(), NULL);
    }

    // Toggle slot
    float slotX = 5;
    float slotW = 4;
    float slotTop = 6;
    float slotBot = box.size.y - 6;
    nvgBeginPath(vg);
    nvgRoundedRect(vg, slotX, slotTop, slotW, slotBot - slotTop, 2);
    nvgFillColor(vg, nvgRGB(15, 15, 15));
    nvgFill(vg);

    // Toggle lever
    float leverY;
    if (state == 0)
      leverY = slotTop + 2;
    else if (state == 2)
      leverY = slotBot - 10;
    else
      leverY = (slotTop + slotBot) / 2 - 4;

    nvgBeginPath(vg);
    nvgRoundedRect(vg, slotX - 2, leverY, slotW + 4, 8, 2);
    NVGpaint metalGrad = nvgLinearGradient(vg, slotX - 2, leverY, slotX + slotW + 2, leverY + 8,
                                           nvgRGB(180, 180, 180), nvgRGB(120, 120, 120));
    nvgFillPaint(vg, metalGrad);
    nvgFill(vg);
    nvgStrokeColor(vg, nvgRGB(60, 60, 60));
    nvgStrokeWidth(vg, 0.5f);
    nvgStroke(vg);

    // Position labels
    float textX = 16;
    float spacing = (slotBot - slotTop) / 2.0f;
    for (int i = 0; i < 3; i++)
    {
      float cy = slotTop + spacing * i;
      nvgFontSize(vg, 6);
      nvgFillColor(vg, nvgRGB(80, 80, 80));
      nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
      nvgText(vg, textX, cy + 2, labels[i], NULL);
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

  // Layout scale: emulator is 505x370, VCV panel is 380px tall
  // Scale to fit height: 380/370 ≈ 1.027, use 1.0 for near-1:1 match
  static constexpr float S = 1.0f;
  static constexpr float MARGIN = 16 * S;

  // Emu layout constants scaled
  static constexpr float MAIN_DW = 256 * S;
  static constexpr float MAIN_DH = 64 * S;
  static constexpr float B_SP = 6 * S;
  static constexpr float DB_SP = 24 * S;
  static constexpr float BTN_W = (256 / 6 - 6) * S;
  static constexpr float BTN_H = BTN_W;
  static constexpr float KNOB_W_S = (256 / 2 - 6) * S;
  static constexpr float KNOB_H_S = 100 * S;
  static constexpr float SUB_DW = 128 * S;
  static constexpr float SUB_DH = 64 * S;
  static constexpr float J_OUTER = 17 * S;
  static constexpr float J_DIVIDER = 50 * S;
  static constexpr float J_HSPACING = 2 * (17 + 8) * S;
  static constexpr float TOGGLE_WS = (256 / 6 - 6 + 6) * S;
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

    j1X = mainDispX + MAIN_DW + J_DIVIDER;
    j2X = j1X + J_HSPACING;
    j3X = j2X + J_HSPACING;
    j4X = j3X + J_HSPACING;

    jb1X = j1X - BTN_W / 2;
    jb1Y = j1Y - BTN_H / 2;
    jb2Y = j2Y - BTN_H / 2;
    jb3Y = j3Y - BTN_H / 2;
    jb4Y = j4Y - BTN_H / 2;

    tStorageX = mb1X - 5 * S;
    tStorageY = j7Y - TOGGLE_HS / 2;
    tModeX = mb3X - 5 * S;
    tModeY = tStorageY;

    ledOut1X = (mainDispX + MAIN_DW + jb1X) / 2;

    // Set panel width to fit everything
    float panelW = j4X + J_OUTER + MARGIN;
    float panelH = RACK_GRID_HEIGHT;
    box.size = Vec(panelW, panelH);

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

  void drawLed(NVGcontext *vg, float x, float y, uint32_t id, int r, int g, int b, const char *label = nullptr, bool side = true)
  {
    float radius = 4 * S;
    bool on = Gpio_read(id);
    nvgBeginPath(vg);
    nvgCircle(vg, x, y, radius);
    if (on)
      nvgFillColor(vg, nvgRGB(r, g, b));
    else
      nvgFillColor(vg, nvgRGB(r / 4, g / 4, b / 4));
    nvgFill(vg);

    if (label)
    {
      nvgFontSize(vg, 6);
      nvgFillColor(vg, nvgRGB(40, 40, 40));
      if (side)
      {
        nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
        nvgText(vg, x - radius - 2, y, label, NULL);
      }
      else
      {
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgText(vg, x, y - radius - 1, label, NULL);
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

    // Silver/light gray panel like real hardware
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, box.size.x, box.size.y);
    nvgFillColor(vg, nvgRGB(210, 210, 212));
    nvgFill(vg);

    // Screws in corners (standard VCV style)
    float screwR = 5;
    float screwInset = 10;
    float screwPositions[][2] = {
        {screwInset, screwInset},
        {box.size.x - screwInset, screwInset},
        {screwInset, box.size.y - screwInset},
        {box.size.x - screwInset, box.size.y - screwInset}};
    for (auto &sp : screwPositions)
    {
      // Screw recess
      nvgBeginPath(vg);
      nvgCircle(vg, sp[0], sp[1], screwR);
      NVGpaint screwGrad = nvgRadialGradient(vg, sp[0] - 1, sp[1] - 1, 0, screwR,
                                              nvgRGB(190, 190, 192), nvgRGB(150, 150, 152));
      nvgFillPaint(vg, screwGrad);
      nvgFill(vg);
      nvgStrokeColor(vg, nvgRGB(140, 140, 140));
      nvgStrokeWidth(vg, 0.5f);
      nvgStroke(vg);
      // Cross slot
      nvgBeginPath(vg);
      nvgMoveTo(vg, sp[0] - 3, sp[1]);
      nvgLineTo(vg, sp[0] + 3, sp[1]);
      nvgMoveTo(vg, sp[0], sp[1] - 3);
      nvgLineTo(vg, sp[0], sp[1] + 3);
      nvgStrokeColor(vg, nvgRGB(120, 120, 120));
      nvgStrokeWidth(vg, 0.75f);
      nvgStroke(vg);
    }

    // Title
    nvgFontSize(vg, 11);
    nvgFillColor(vg, nvgRGB(40, 40, 40));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, mainDispX + 40, 3, "ER-301", NULL);
    nvgFontSize(vg, 8);
    nvgFillColor(vg, nvgRGB(100, 100, 100));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgText(vg, mainDispX + 88, 4.5f, "SOUND COMPUTER", NULL);

    // Jack column headers
    nvgFontSize(vg, 7);
    nvgFillColor(vg, nvgRGB(80, 80, 80));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgText(vg, j2X, j1Y - J_OUTER + 2, "G", NULL);
    nvgText(vg, j3X, j1Y - J_OUTER + 2, "IN", NULL);
    nvgText(vg, j4X, j1Y - J_OUTER + 2, "OUT", NULL);

    // Channel numbers on select buttons
    nvgFontSize(vg, 9);
    nvgFillColor(vg, nvgRGB(220, 220, 220));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgText(vg, jb1X + BTN_W / 2, jb1Y + BTN_H / 2, "1", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb2Y + BTN_H / 2, "2", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb3Y + BTN_H / 2, "3", NULL);
    nvgText(vg, jb1X + BTN_W / 2, jb4Y + BTN_H / 2, "4", NULL);

    // CV jack labels (A1-D3)
    nvgFontSize(vg, 6);
    nvgFillColor(vg, nvgRGB(80, 80, 80));
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
        nvgText(vg, jxArr[col], jyArr[row] - 11, buf, NULL);
      }
    }

    // Bottom voltage specs
    nvgFontSize(vg, 5.5f);
    nvgFillColor(vg, nvgRGB(100, 100, 100));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    float bottomY = box.size.y - 4;
    nvgText(vg, box.size.x / 2, bottomY, "G: 0V-10V  |  IN+ABCD: -10V to 10V  |  OUT: -7V to 7V", NULL);

    // === Main Display ===
    nvgBeginPath(vg);
    nvgRect(vg, mainDispX - 1, mainDispY - 1, MAIN_DW + 2, MAIN_DH + 2);
    nvgStrokeColor(vg, nvgRGB(0, 0, 0));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

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

    // === Sub Display ===
    nvgBeginPath(vg);
    nvgRect(vg, subDispX - 1, subDispY - 1, SUB_DW + 2, SUB_DH + 2);
    nvgStrokeColor(vg, nvgRGB(0, 0, 0));
    nvgStrokeWidth(vg, 1.0f);
    nvgStroke(vg);

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

    // === LEDs ===
    // Fine/Coarse LEDs near knob
    float ledDial1X = mb1X + 4 * S + 4 * S;
    float ledDial1Y = subDispY + SUB_DH - 2 * 4 * S;
    float ledDial2X = ledDial1X + BTN_W / 2;
    float ledDial2Y = ledDial1Y + BTN_H / 2;
    drawLed(vg, ledDial1X, ledDial1Y, LED_DIAL1, 230, 0, 0, "fine");
    drawLed(vg, ledDial2X, ledDial2Y, LED_DIAL2, 230, 0, 0, "coarse");

    // I/O and Safe LEDs
    float ledIOX = mb2X + 4 * S;
    float ledIOY = j7Y - BTN_W / 4;
    float ledSafeY = j7Y + BTN_W / 4;
    drawLed(vg, ledIOX, ledIOY, LED_IO, 230, 0, 0, "I/O");
    drawLed(vg, ledIOX, ledSafeY, LED_SAFE, 230, 0, 0, "safe");

    // Output LEDs (orange)
    drawLed(vg, ledOut1X, jb1Y + BTN_H / 2, LED_OUT1, 220, 110, 0);
    drawLed(vg, ledOut1X, jb2Y + BTN_H / 2, LED_OUT2, 220, 110, 0);
    drawLed(vg, ledOut1X, jb3Y + BTN_H / 2, LED_OUT3, 220, 110, 0);
    drawLed(vg, ledOut1X, jb4Y + BTN_H / 2, LED_OUT4, 220, 110, 0);

    // Link LEDs (red)
    drawLed(vg, ledOut1X, (jb1Y + jb2Y) / 2 + BTN_W / 2, LED_LINK12, 230, 0, 0, "linked");
    drawLed(vg, ledOut1X, (jb2Y + jb3Y) / 2 + BTN_W / 2, LED_LINK23, 230, 0, 0, "linked");
    drawLed(vg, ledOut1X, (jb3Y + jb4Y) / 2 + BTN_W / 2, LED_LINK34, 230, 0, 0, "linked");

    // === Section divider lines (subtle, like real hardware) ===
    // Storage section
    float stOutX = MARGIN / 2;
    float stOutY = tStorageY - 16 * S;
    float stOutW = (mb2X + mb3X) / 2 - MARGIN / 2 + 12 * S;
    float stOutH = MARGIN / 2 + box.size.y - MARGIN - stOutY;
    nvgBeginPath(vg);
    nvgRect(vg, stOutX, stOutY, stOutW, stOutH);
    nvgStrokeColor(vg, nvgRGB(150, 150, 150));
    nvgStrokeWidth(vg, 0.75f);
    nvgStroke(vg);

    // ABCD jack section
    float abcdX = ledOut1X;
    float abcdY = (j4Y + j5Y) / 2;
    float abcdW = MARGIN / 2 + box.size.x - MARGIN - abcdX;
    float abcdH = MARGIN / 2 + box.size.y - MARGIN - abcdY;
    nvgBeginPath(vg);
    nvgRect(vg, abcdX, abcdY, abcdW, abcdH);
    nvgStrokeColor(vg, nvgRGB(150, 150, 150));
    nvgStrokeWidth(vg, 0.75f);
    nvgStroke(vg);

    // I/O label near LED
    nvgFontSize(vg, 7);
    nvgFillColor(vg, nvgRGB(40, 40, 40));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    float ioLblX = (mb2X + mb3X) / 2 + 8 * S;
    nvgText(vg, ioLblX, ledIOY, "I/O", NULL);
    nvgText(vg, ioLblX, ledSafeY, "safe", NULL);

    ModuleWidget::draw(args);
  }
};

Model *modelER301 = createModel<ER301Module, ER301Widget>("ER301");
