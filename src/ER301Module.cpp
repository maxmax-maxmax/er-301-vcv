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
    // PWM channel order: A1,B1,C1,D1, A2,B2,C2,D2, A3,B3,C3,D3
    static const int cvLightIds[] = {
        LIGHT_CV_A1_GREEN, LIGHT_CV_B1_GREEN, LIGHT_CV_C1_GREEN, LIGHT_CV_D1_GREEN,
        LIGHT_CV_A2_GREEN, LIGHT_CV_B2_GREEN, LIGHT_CV_C2_GREEN, LIGHT_CV_D2_GREEN,
        LIGHT_CV_A3_GREEN, LIGHT_CV_B3_GREEN, LIGHT_CV_C3_GREEN, LIGHT_CV_D3_GREEN};
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
  bool isOutlined; // outlined style for select buttons (like real panel)
  std::string topLabel;  // label above button (M1, CANCEL, etc.)
  std::string botLabel;  // label below top label (QUICKSAVE, etc.)
  bool pressed = false;

  ER301Button(uint32_t id, Vec pos, Vec size, bool blue, const char *top = "", const char *bot = "", bool outlined = false)
      : gpioId(id), isBlue(blue), isOutlined(outlined), topLabel(top), botLabel(bot)
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

    if (isOutlined)
    {
      // Outlined select button (like real panel silkscreen)
      if (pressed)
      {
        nvgBeginPath(vg);
        nvgRoundedRect(vg, 0, 0, w, h, r);
        nvgFillColor(vg, nvgRGBA(0, 0, 0, 25));
        nvgFill(vg);
      }

      // Thin border
      nvgBeginPath(vg);
      nvgRoundedRect(vg, 1, 1, w - 2, h - 2, r);
      nvgStrokeColor(vg, nvgRGB(90, 90, 90));
      nvgStrokeWidth(vg, 0.75f);
      nvgStroke(vg);

      // Number inside the button
      if (!topLabel.empty())
      {
        nvgFontSize(vg, 11);
        nvgFillColor(vg, nvgRGB(50, 50, 50));
        nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(vg, w / 2, h / 2, topLabel.c_str(), NULL);
      }
      return; // skip normal label drawing for outlined buttons
    }
    else
    {
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
    }

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
      // Draw pill-shaped background (like real panel silkscreen)
      nvgFontSize(vg, 6.0f);
      float bounds[4];
      nvgTextBounds(vg, w / 2, -14, botLabel.c_str(), NULL, bounds);
      float tw = bounds[2] - bounds[0];
      float pillW = tw + 6;
      float pillH = 9;
      float pillX = w / 2 - pillW / 2;
      float pillY = -19;
      nvgBeginPath(vg);
      nvgRoundedRect(vg, pillX, pillY, pillW, pillH, 3);
      nvgStrokeColor(vg, nvgRGB(80, 80, 80));
      nvgStrokeWidth(vg, 0.75f);
      nvgStroke(vg);

      nvgFillColor(vg, nvgRGB(60, 60, 60));
      nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
      nvgText(vg, w / 2, pillY + pillH / 2, botLabel.c_str(), NULL);
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
  int mainImage = -1;
  int subImage = -1;
  uint8_t mainPixels[MAIN_HORIZONTAL_PIXELS * MAIN_VERTICAL_PIXELS * 4];
  uint8_t subPixels[SUB_HORIZONTAL_PIXELS * SUB_VERTICAL_PIXELS * 4];

  static constexpr int SCREEN_BRIGHTNESS = 15;
  static constexpr float SCREEN_TINT = 0.85f;

  // Display render sizes (scaled to fit 30HP; pixel buffers remain 256x64 / 128x64)
  static constexpr float MAIN_DW = 234;
  static constexpr float MAIN_DH = 58;
  static constexpr float SUB_DW = 117;
  static constexpr float SUB_DH = 58;

  // Button/UI sizes
  static constexpr float BTN_W = 26;
  static constexpr float BTN_H = 26;
  static constexpr float TOGGLE_W = 38;
  static constexpr float TOGGLE_H = 34;
  static constexpr float J_HSPACING = 46;

  // All positions stored as members for draw() access
  float panelW;
  float mainDispX, mainDispY;
  float mb1X, mb2X, mb3X, mb4X, mb5X, mb6X, mbY;
  float knobX, knobY;
  float subDispX, subDispY;
  float fineX, fineY, coarseY;
  float sbY; // dial/sub button row Y
  float hbY; // hard button row Y
  float tStorageX, tStorageY, tModeX, tModeY;
  float ioLedX, ioLedY, safeLedY;
  float j1X, j2X, j3X, j4X;
  float j1Y, j2Y, j3Y, j4Y, j5Y, j6Y, j7Y;
  float selBtnX;
  float outLedX;

  ER301Widget(ER301Module *module)
  {
    setModule(module);
    memset(mainPixels, 0, sizeof(mainPixels));
    memset(subPixels, 0, sizeof(subPixels));

    // ════════════════════════════════════════════════════════════════
    // PANEL LAYOUT — Panel is 380px tall.
    // Mapped from real ER-301 photo proportions.
    // Left section: displays, buttons, knob, toggles (~272px wide)
    // Right section: jacks in columns (~240px wide)
    // ════════════════════════════════════════════════════════════════

    // 30HP panel = 457.2px wide
    panelW = 30 * RACK_GRID_WIDTH;
    box.size = Vec(panelW, RACK_GRID_HEIGHT);

    float LM = 10; // left margin
    float RM = 10; // right margin
    mainDispX = LM;

    // Jack columns — fit 4 columns in right section
    // Right section starts after display + gap
    j1X = LM + MAIN_DW + 28;         // select btn column
    j4X = panelW - RM - 12;          // OUT column (rightmost)
    float jackSpan = j4X - j1X;      // total span for 3 gaps
    float jHS = jackSpan / 3.0f;     // computed jack column spacing
    j2X = j1X + jHS;                 // G column
    j3X = j2X + jHS;                 // IN column

    // ── LEFT PANEL vertical layout (all Y = top edge of element) ──
    // Budget: 380px total
    //   18  title area (screws + title text)
    //   64  main display
    //   20  gap (pills + M labels)
    //   28  M buttons
    //    8  gap
    //   84  knob (smaller to fit)
    //    4  gap
    //   12  fine/coarse LEDs
    //    6  gap (HOME pill + label)
    //   28  dial/sub buttons
    //   14  gap (COMMIT pill + label + divider)
    //   28  hard buttons / toggles
    //   28  toggle labels area
    //   10  bottom specs
    //  = 352 content + 28 padding = 380 ✓

    mainDispY = 20;                    // Y=20..84
    mbY = 104;                         // Y=104..132 (gap=20 for pills+labels)
    knobX = LM;
    knobY = 142;                       // Y=142..226 (knob 84px tall)
    subDispX = LM + MAIN_DW / 2;
    subDispY = knobY + (84 - SUB_DH) / 2; // centered in knob area

    fineX = LM + 26;
    fineY = 232;                       // Y=232
    coarseY = 244;                     // Y=244

    sbY = 256;                         // Y=256..284
    hbY = 300;                         // Y=300..328

    tStorageX = LM - 1;
    tStorageY = hbY;
    tModeX = LM + 2 * 42 - 1;
    tModeY = hbY;

    ioLedX = tStorageX + TOGGLE_W + 10;
    ioLedY = tStorageY + 10;
    safeLedY = tStorageY + TOGGLE_H - 6;

    // ── M button X positions (6 across 256px) ──
    float gridStep = 42;
    mb1X = LM + 7;
    mb2X = mb1X + gridStep;
    mb3X = mb2X + gridStep;
    mb4X = mb3X + gridStep;
    mb5X = mb4X + gridStep;
    mb6X = mb5X + gridStep;

    // ── RIGHT PANEL vertical layout ──
    // Upper: rows 1-4 for select/G/IN/OUT (Y=38..220, spacing=60)
    j1Y = 40;
    j2Y = 100;
    j3Y = 160;
    j4Y = 220;

    // ABCD divider at ~240
    // Lower: rows 5-7 for ABCD (Y=262..348, spacing=43)
    j5Y = 262;
    j6Y = 305;
    j7Y = 348;

    selBtnX = j1X - BTN_W / 2;
    outLedX = (LM + MAIN_DW + selBtnX) / 2;

    // ════════════════════════════════════════════════════════════════
    // WIDGETS
    // ════════════════════════════════════════════════════════════════

    // Screws
    addChild(createWidget<ScrewSilver>(Vec(0, 0)));
    addChild(createWidget<ScrewSilver>(Vec(panelW - RACK_GRID_WIDTH, 0)));
    addChild(createWidget<ScrewSilver>(Vec(0, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
    addChild(createWidget<ScrewSilver>(Vec(panelW - RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    // M1-M6 buttons
    addChild(new ER301Button(BUTTON_MAIN1, Vec(mb1X, mbY), Vec(BTN_W, BTN_H), false, "M1", "QUICKSAVE"));
    addChild(new ER301Button(BUTTON_MAIN2, Vec(mb2X, mbY), Vec(BTN_W, BTN_H), false, "M2"));
    addChild(new ER301Button(BUTTON_MAIN3, Vec(mb3X, mbY), Vec(BTN_W, BTN_H), false, "M3"));
    addChild(new ER301Button(BUTTON_MAIN4, Vec(mb4X, mbY), Vec(BTN_W, BTN_H), false, "M4"));
    addChild(new ER301Button(BUTTON_MAIN5, Vec(mb5X, mbY), Vec(BTN_W, BTN_H), false, "M5"));
    addChild(new ER301Button(BUTTON_MAIN6, Vec(mb6X, mbY), Vec(BTN_W, BTN_H), false, "M6", "FOCUS"));

    // Dial/Sub buttons
    addChild(new ER301Button(BUTTON_DIAL1, Vec(mb1X, sbY), Vec(BTN_W, BTN_H), true));
    addChild(new ER301Button(BUTTON_DIAL2, Vec(mb2X, sbY), Vec(BTN_W, BTN_H), true, "CANCEL"));
    addChild(new ER301Button(BUTTON_DIAL3, Vec(mb3X, sbY), Vec(BTN_W, BTN_H), true, "ZERO", "HOME"));
    addChild(new ER301Button(BUTTON_SUB1, Vec(mb4X, sbY), Vec(BTN_W, BTN_H), false, "S1"));
    addChild(new ER301Button(BUTTON_SUB2, Vec(mb5X, sbY), Vec(BTN_W, BTN_H), false, "S2"));
    addChild(new ER301Button(BUTTON_SUB3, Vec(mb6X, sbY), Vec(BTN_W, BTN_H), false, "S3", "FOCUS"));

    // Hard buttons
    addChild(new ER301Button(BUTTON_ENTER, Vec(mb4X, hbY), Vec(BTN_W, BTN_H), true, "ENTER", "COMMIT"));
    addChild(new ER301Button(BUTTON_UP, Vec(mb5X, hbY), Vec(BTN_W, BTN_H), true, "UP"));
    addChild(new ER301Button(BUTTON_SHIFT, Vec(mb6X, hbY), Vec(BTN_W, BTN_H), true, "SHIFT"));

    // Select buttons 1-4 (outlined, numbers inside)
    addChild(new ER301Button(BUTTON_SELECT1, Vec(selBtnX, j1Y - BTN_H / 2), Vec(BTN_W, BTN_H), false, "1", "", true));
    addChild(new ER301Button(BUTTON_SELECT2, Vec(selBtnX, j2Y - BTN_H / 2), Vec(BTN_W, BTN_H), false, "2", "", true));
    addChild(new ER301Button(BUTTON_SELECT3, Vec(selBtnX, j3Y - BTN_H / 2), Vec(BTN_W, BTN_H), false, "3", "", true));
    addChild(new ER301Button(BUTTON_SELECT4, Vec(selBtnX, j4Y - BTN_H / 2), Vec(BTN_W, BTN_H), false, "4", "", true));

    // Encoder knob (84px tall to fit layout)
    addChild(new ER301Knob(Vec(knobX, knobY), Vec(KNOB_W, 84)));

    // Toggle switches
    addChild(new ER301Toggle(TOGGLE_STORAGE_A, TOGGLE_STORAGE_B,
                             Vec(tStorageX, tStorageY), Vec(TOGGLE_W, TOGGLE_H),
                             "STORAGE", "user", "admin", "eject"));
    addChild(new ER301Toggle(TOGGLE_MODE_A, TOGGLE_MODE_B,
                             Vec(tModeX, tModeY), Vec(TOGGLE_W, TOGGLE_H),
                             "MODE", "hold", "edit", "scope"));

    // ── LEDs ──

    // Fine/Coarse
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(fineX, fineY), module, ER301Module::LIGHT_DIAL1));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(fineX, coarseY), module, ER301Module::LIGHT_DIAL2));

    // I/O, Safe
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ioLedX, ioLedY), module, ER301Module::LIGHT_IO));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ioLedX, safeLedY), module, ER301Module::LIGHT_SAFE));

    // Output LEDs
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, j1Y), module, ER301Module::LIGHT_OUT1));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, j2Y), module, ER301Module::LIGHT_OUT2));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, j3Y), module, ER301Module::LIGHT_OUT3));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, j4Y), module, ER301Module::LIGHT_OUT4));

    // Link LEDs
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (j1Y + j2Y) / 2), module, ER301Module::LIGHT_LINK12));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (j2Y + j3Y) / 2), module, ER301Module::LIGHT_LINK23));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (j3Y + j4Y) / 2), module, ER301Module::LIGHT_LINK34));

    // CV input LEDs (green/red bicolor, upper-left of each jack)
    static const int cvGreenIds[] = {
        ER301Module::LIGHT_CV_A1_GREEN, ER301Module::LIGHT_CV_A2_GREEN, ER301Module::LIGHT_CV_A3_GREEN,
        ER301Module::LIGHT_CV_B1_GREEN, ER301Module::LIGHT_CV_B2_GREEN, ER301Module::LIGHT_CV_B3_GREEN,
        ER301Module::LIGHT_CV_C1_GREEN, ER301Module::LIGHT_CV_C2_GREEN, ER301Module::LIGHT_CV_C3_GREEN,
        ER301Module::LIGHT_CV_D1_GREEN, ER301Module::LIGHT_CV_D2_GREEN, ER301Module::LIGHT_CV_D3_GREEN};
    float cvJx[] = {j1X, j2X, j3X, j4X};
    float cvJy[] = {j5Y, j6Y, j7Y};
    for (int col = 0; col < 4; col++)
      for (int row = 0; row < 3; row++)
        addChild(createLightCentered<MediumLight<GreenRedLight>>(
            Vec(cvJx[col] - 15, cvJy[row] - 15), module, cvGreenIds[col * 3 + row]));

    // ── Jack ports ──

    // G1-G4
    for (int i = 0; i < 4; i++)
      addInput(createInputCentered<PJ301MPort>(Vec(j2X, (&j1Y)[i]), module, ER301Module::INPUT_G1_PORT + i));

    // IN1-IN4
    for (int i = 0; i < 4; i++)
      addInput(createInputCentered<PJ301MPort>(Vec(j3X, (&j1Y)[i]), module, ER301Module::INPUT_IN1_PORT + i));

    // OUT1-OUT4
    for (int i = 0; i < 4; i++)
      addOutput(createOutputCentered<PJ301MPort>(Vec(j4X, (&j1Y)[i]), module, ER301Module::OUTPUT_OUT1 + i));

    // ABCD CV inputs
    int cvInputIds[4][3] = {
        {ER301Module::INPUT_A1_PORT, ER301Module::INPUT_A2_PORT, ER301Module::INPUT_A3_PORT},
        {ER301Module::INPUT_B1_PORT, ER301Module::INPUT_B2_PORT, ER301Module::INPUT_B3_PORT},
        {ER301Module::INPUT_C1_PORT, ER301Module::INPUT_C2_PORT, ER301Module::INPUT_C3_PORT},
        {ER301Module::INPUT_D1_PORT, ER301Module::INPUT_D2_PORT, ER301Module::INPUT_D3_PORT}};
    for (int col = 0; col < 4; col++)
      for (int row = 0; row < 3; row++)
        addInput(createInputCentered<PJ301MPort>(Vec(cvJx[col], cvJy[row]), module, cvInputIds[col][row]));
  }

  ~ER301Widget() {}

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

    // Trigger display update
    ER301Module *mod = dynamic_cast<ER301Module *>(module);
    if (mod && mod->audioReady.load(std::memory_order_acquire))
      Events_push(EVENT_DISPLAY_READY);

    float pw = box.size.x;
    float ph = box.size.y;

    // ── Panel background ──
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

    // ── Title ──
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFontSize(vg, 14);
    nvgFillColor(vg, nvgRGB(30, 30, 30));
    nvgText(vg, pw / 2 - 40, 12, "ER-301", NULL);
    nvgFontSize(vg, 9.5f);
    nvgFillColor(vg, nvgRGB(60, 60, 60));
    nvgText(vg, pw / 2 + 55, 12, "SOUND COMPUTER", NULL);

    // ── Main Display bezel ──
    nvgBeginPath(vg);
    nvgRoundedRect(vg, mainDispX - 3, mainDispY - 3, MAIN_DW + 6, MAIN_DH + 6, 4);
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

    // ── Sub Display bezel ──
    nvgBeginPath(vg);
    nvgRoundedRect(vg, subDispX - 3, subDispY - 3, SUB_DW + 6, SUB_DH + 6, 4);
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

    // ── Fine/Coarse labels ──
    nvgFontSize(vg, 6.5f);
    nvgFillColor(vg, nvgRGB(50, 50, 50));
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
    nvgText(vg, fineX - 7, fineY, "fine", NULL);
    nvgText(vg, fineX - 7, coarseY, "coarse", NULL);

    // ── I/O and Safe labels ──
    nvgFontSize(vg, 6.5f);
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, ioLedX + 7, ioLedY, "I/O", NULL);
    nvgText(vg, ioLedX + 7, safeLedY, "safe", NULL);

    // ── Storage/mode divider line ──
    nvgStrokeColor(vg, nvgRGB(165, 165, 167));
    nvgStrokeWidth(vg, 0.5f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, 4, hbY - 8);
    nvgLineTo(vg, mainDispX + MAIN_DW, hbY - 8);
    nvgStroke(vg);

    // ── G/IN/OUT column headers ──
    nvgFontSize(vg, 9);
    nvgFillColor(vg, nvgRGB(50, 50, 50));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    nvgText(vg, j2X, j1Y - 20, "G", NULL);
    nvgText(vg, j3X, j1Y - 20, "IN", NULL);
    nvgText(vg, j4X, j1Y - 20, "OUT", NULL);

    // ── Dashed vertical dividers between jack columns ──
    {
      float dTop = j1Y - 16;
      float dBot = j4Y + 18;
      float dxs[] = {(j1X + j2X) / 2, (j2X + j3X) / 2, (j3X + j4X) / 2};
      nvgStrokeColor(vg, nvgRGB(165, 165, 167));
      nvgStrokeWidth(vg, 0.5f);
      for (int d = 0; d < 3; d++)
        for (float y = dTop; y < dBot; y += 6)
        {
          nvgBeginPath(vg);
          nvgMoveTo(vg, dxs[d], y);
          nvgLineTo(vg, dxs[d], std::min(y + 3.0f, dBot));
          nvgStroke(vg);
        }
    }

    // ── "linked" labels ──
    nvgFontSize(vg, 6.5f);
    nvgFillColor(vg, nvgRGB(60, 60, 60));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
    nvgText(vg, outLedX + 7, (j1Y + j2Y) / 2, "linked", NULL);
    nvgText(vg, outLedX + 7, (j2Y + j3Y) / 2, "linked", NULL);
    nvgText(vg, outLedX + 7, (j3Y + j4Y) / 2, "linked", NULL);

    // ── ABCD section divider ──
    float abcdDivY = (j4Y + j5Y) / 2;
    nvgStrokeColor(vg, nvgRGB(165, 165, 167));
    nvgStrokeWidth(vg, 0.5f);
    nvgBeginPath(vg);
    nvgMoveTo(vg, j1X - 28, abcdDivY);
    nvgLineTo(vg, pw - 4, abcdDivY);
    nvgStroke(vg);

    // ── CV jack labels (A1-D3) ──
    nvgFontSize(vg, 8);
    nvgFillColor(vg, nvgRGB(60, 60, 60));
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
    const char *cols[] = {"A", "B", "C", "D"};
    float jxA[] = {j1X, j2X, j3X, j4X};
    float jyA[] = {j5Y, j6Y, j7Y};
    for (int c = 0; c < 4; c++)
      for (int r = 0; r < 3; r++)
      {
        char lbl[4];
        snprintf(lbl, sizeof(lbl), "%s%d", cols[c], r + 1);
        nvgText(vg, jxA[c], jyA[r] - 14, lbl, NULL);
      }

    // ── Bottom specs ──
    nvgFontSize(vg, 5.5f);
    nvgFillColor(vg, nvgRGB(40, 40, 40));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
    nvgText(vg, mainDispX, ph - 5, "G: 0V-10V | Fs: 96kHz | 12-bit", NULL);
    nvgText(vg, (j1X + j2X) / 2, ph - 5, "IN+ABCD: -10V-10V | Fs: 60kHz | 16-bit", NULL);
    nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
    nvgText(vg, pw - 16, ph - 5, "OUT: -7V-7V | Fs: 96kHz | 24-bit", NULL);

    ModuleWidget::draw(args);
  }
};

Model *modelER301 = createModel<ER301Module, ER301Widget>("ER301");
