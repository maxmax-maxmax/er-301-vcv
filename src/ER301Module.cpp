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

// ─── Transparent button overlay (PNG provides visuals) ───
struct ER301Button : OpaqueWidget
{
  uint32_t gpioId;
  bool pressed = false;

  ER301Button(uint32_t id, Vec pos, Vec size)
      : gpioId(id)
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
    // Transparent — only show press feedback
    if (pressed)
    {
      NVGcontext *vg = args.vg;
      nvgBeginPath(vg);
      nvgRoundedRect(vg, 0, 0, box.size.x, box.size.y, 2);
      nvgFillColor(vg, nvgRGBA(255, 255, 255, 40));
      nvgFill(vg);
    }
  }
};

// ─── Toggle switch overlay (3-position, transparent) ───
struct ER301Toggle : OpaqueWidget
{
  uint32_t idA, idB;

  ER301Toggle(uint32_t a, uint32_t b, Vec pos, Vec size)
      : idA(a), idB(b)
  {
    box.pos = pos;
    box.size = size;
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
    // Transparent — PNG provides the visual
  }
};

// ─── Encoder knob overlay (transparent, handles drag/scroll) ───
struct ER301Knob : OpaqueWidget
{
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
    // Transparent — PNG provides the visual
  }
};

struct ER301Widget : ModuleWidget
{
  int mainImage = -1;
  int subImage = -1;
  int panelImage = -1;
  uint8_t mainPixels[MAIN_HORIZONTAL_PIXELS * MAIN_VERTICAL_PIXELS * 4];
  uint8_t subPixels[SUB_HORIZONTAL_PIXELS * SUB_VERTICAL_PIXELS * 4];

  static constexpr int SCREEN_BRIGHTNESS = 15;
  static constexpr float SCREEN_TINT = 0.85f;

  // ════════════════════════════════════════════════════════════════
  // All positions mapped from Panel.png (2246x1888 → 457.2x380)
  // Image scale: sx = 457.2/2246 = 0.20356, sy = 380/1888 = 0.20127
  // ════════════════════════════════════════════════════════════════

  float panelW;

  // Display positions and sizes (where to render pixel buffers on top of PNG)
  float mainDispX, mainDispY, mainDispW, mainDispH;
  float subDispX, subDispY, subDispW, subDispH;

  // Button positions (center X, top-left Y for widget placement)
  float mbX[6]; // M1-M6 / sub buttons X positions (center)
  float mbY;    // M button row Y (center)
  float sbY;    // Sub/dial button row Y (center)
  float hbY;    // Hard button row Y (center)

  // Button size
  static constexpr float BTN_W = 22;
  static constexpr float BTN_H = 22;

  // Knob
  float knobCX, knobCY, knobR;

  // Jack positions (centers)
  float jGX, jINX, jOUTX;           // Upper section: G, IN, OUT columns
  float jUpperY[4];                  // Upper section: rows 1-4
  float jAX, jBX, jCX, jDX;         // ABCD columns
  float jABCDY[3];                   // ABCD rows 1-3

  // Select button column
  float selBtnX;

  // Output/link LED X
  float outLedX;

  // Toggle positions
  float tStorageX, tStorageY;
  float tModeX, tModeY;
  static constexpr float TOGGLE_W = 36;
  static constexpr float TOGGLE_H = 32;

  // Fine/coarse LED positions
  float fineX, fineY, coarseY;

  // I/O and safe LED positions
  float ioLedX, ioLedY, safeLedY;

  ER301Widget(ER301Module *module)
  {
    setModule(module);
    memset(mainPixels, 0, sizeof(mainPixels));
    memset(subPixels, 0, sizeof(subPixels));

    // ════════════════════════════════════════════════════════════════
    // PANEL LAYOUT — All positions mapped from Panel.png analysis
    // PNG is 2246x1888, rendered at 457.2x380 (30HP)
    // ════════════════════════════════════════════════════════════════

    panelW = 30 * RACK_GRID_WIDTH; // 457.2
    box.size = Vec(panelW, RACK_GRID_HEIGHT);

    // ── M button X centers (from image scan at button row) ──
    mbX[0] = 26.8f;   // M1
    mbX[1] = 69.8f;   // M2
    mbX[2] = 113.3f;  // M3
    mbX[3] = 156.6f;  // M4
    mbX[4] = 200.1f;  // M5
    mbX[5] = 243.5f;  // M6

    // ── Button row Y centers ──
    mbY  = 147.0f;
    sbY  = 290.5f;
    hbY  = 338.0f;

    // ── Knob ──
    knobCX = 84.0f;
    knobCY = 213.0f;
    knobR  = 32.0f;

    // ── Displays (inner screen area within PNG bezel) ──
    mainDispX = 20.0f;
    mainDispY = 50.0f;
    mainDispW = 230.0f;
    mainDispH = 42.0f;

    subDispX = 128.0f;
    subDispY = 194.0f;
    subDispW = 120.0f;
    subDispH = 48.0f;

    // ── Jack columns (upper section: G, IN, OUT) ──
    jGX   = 350.5f;
    jINX  = 393.9f;
    jOUTX = 436.8f;

    // ── Jack rows (upper section) ──
    jUpperY[0] = 50.0f;   // Row 1
    jUpperY[1] = 97.5f;   // Row 2
    jUpperY[2] = 145.4f;  // Row 3
    jUpperY[3] = 193.2f;  // Row 4

    // ── ABCD jack columns ──
    jAX = 307.0f;
    jBX = 350.5f;
    jCX = 393.5f;
    jDX = 436.7f;

    // ── ABCD jack rows ──
    jABCDY[0] = 241.5f;   // Row 1
    jABCDY[1] = 290.1f;   // Row 2
    jABCDY[2] = 337.8f;   // Row 3

    // ── Select buttons (left of G column) ──
    selBtnX = 256.0f;

    // ── Output/link LEDs ──
    outLedX = 275.0f;

    // ── Toggle switches ──
    tStorageX = 8.0f;
    tStorageY = hbY - 16.0f;
    tModeX = 90.0f;
    tModeY = tStorageY;

    // ── Fine/coarse LEDs ──
    fineX = 30.0f;
    fineY = 253.0f;
    coarseY = 265.0f;

    // ── I/O and safe LEDs ──
    ioLedX = 74.0f;
    ioLedY = tStorageY + 9;
    safeLedY = tStorageY + 24;

    // ════════════════════════════════════════════════════════════════
    // WIDGETS — transparent overlays on top of PNG background
    // ════════════════════════════════════════════════════════════════

    // M1-M6 buttons (grey in image)
    addChild(new ER301Button(BUTTON_MAIN1, Vec(mbX[0] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_MAIN2, Vec(mbX[1] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_MAIN3, Vec(mbX[2] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_MAIN4, Vec(mbX[3] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_MAIN5, Vec(mbX[4] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_MAIN6, Vec(mbX[5] - BTN_W/2, mbY - BTN_H/2), Vec(BTN_W, BTN_H)));

    // Dial/Sub buttons (first 3 blue, last 3 grey in image)
    addChild(new ER301Button(BUTTON_DIAL1, Vec(mbX[0] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_DIAL2, Vec(mbX[1] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_DIAL3, Vec(mbX[2] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SUB1,  Vec(mbX[3] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SUB2,  Vec(mbX[4] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SUB3,  Vec(mbX[5] - BTN_W/2, sbY - BTN_H/2), Vec(BTN_W, BTN_H)));

    // Hard buttons (ENTER, UP, SHIFT — blue in image)
    addChild(new ER301Button(BUTTON_ENTER, Vec(mbX[3] - BTN_W/2, hbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_UP,    Vec(mbX[4] - BTN_W/2, hbY - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SHIFT, Vec(mbX[5] - BTN_W/2, hbY - BTN_H/2), Vec(BTN_W, BTN_H)));

    // Select buttons 1-4
    addChild(new ER301Button(BUTTON_SELECT1, Vec(selBtnX - BTN_W/2, jUpperY[0] - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SELECT2, Vec(selBtnX - BTN_W/2, jUpperY[1] - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SELECT3, Vec(selBtnX - BTN_W/2, jUpperY[2] - BTN_H/2), Vec(BTN_W, BTN_H)));
    addChild(new ER301Button(BUTTON_SELECT4, Vec(selBtnX - BTN_W/2, jUpperY[3] - BTN_H/2), Vec(BTN_W, BTN_H)));

    // Encoder knob (circular hit area)
    float knobDia = knobR * 2;
    addChild(new ER301Knob(Vec(knobCX - knobR, knobCY - knobR), Vec(knobDia, knobDia)));

    // Toggle switches
    addChild(new ER301Toggle(TOGGLE_STORAGE_A, TOGGLE_STORAGE_B,
                             Vec(tStorageX, tStorageY), Vec(TOGGLE_W, TOGGLE_H)));
    addChild(new ER301Toggle(TOGGLE_MODE_A, TOGGLE_MODE_B,
                             Vec(tModeX, tModeY), Vec(TOGGLE_W, TOGGLE_H)));

    // ── LEDs ──

    // Fine/Coarse
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(fineX, fineY), module, ER301Module::LIGHT_DIAL1));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(fineX, coarseY), module, ER301Module::LIGHT_DIAL2));

    // I/O, Safe
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ioLedX, ioLedY), module, ER301Module::LIGHT_IO));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(ioLedX, safeLedY), module, ER301Module::LIGHT_SAFE));

    // Output LEDs
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, jUpperY[0]), module, ER301Module::LIGHT_OUT1));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, jUpperY[1]), module, ER301Module::LIGHT_OUT2));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, jUpperY[2]), module, ER301Module::LIGHT_OUT3));
    addChild(createLightCentered<MediumLight<YellowLight>>(Vec(outLedX, jUpperY[3]), module, ER301Module::LIGHT_OUT4));

    // Link LEDs (between output rows)
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (jUpperY[0] + jUpperY[1]) / 2), module, ER301Module::LIGHT_LINK12));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (jUpperY[1] + jUpperY[2]) / 2), module, ER301Module::LIGHT_LINK23));
    addChild(createLightCentered<MediumLight<RedLight>>(Vec(outLedX, (jUpperY[2] + jUpperY[3]) / 2), module, ER301Module::LIGHT_LINK34));

    // CV input LEDs (green/red bicolor, upper-left of each jack)
    static const int cvGreenIds[] = {
        ER301Module::LIGHT_CV_A1_GREEN, ER301Module::LIGHT_CV_A2_GREEN, ER301Module::LIGHT_CV_A3_GREEN,
        ER301Module::LIGHT_CV_B1_GREEN, ER301Module::LIGHT_CV_B2_GREEN, ER301Module::LIGHT_CV_B3_GREEN,
        ER301Module::LIGHT_CV_C1_GREEN, ER301Module::LIGHT_CV_C2_GREEN, ER301Module::LIGHT_CV_C3_GREEN,
        ER301Module::LIGHT_CV_D1_GREEN, ER301Module::LIGHT_CV_D2_GREEN, ER301Module::LIGHT_CV_D3_GREEN};
    float cvJx[] = {jAX, jBX, jCX, jDX};
    for (int col = 0; col < 4; col++)
      for (int row = 0; row < 3; row++)
        addChild(createLightCentered<MediumLight<GreenRedLight>>(
            Vec(cvJx[col] - 15, jABCDY[row] - 15), module, cvGreenIds[col * 3 + row]));

    // ── Jack ports ──

    // G1-G4
    for (int i = 0; i < 4; i++)
      addInput(createInputCentered<PJ301MPort>(Vec(jGX, jUpperY[i]), module, ER301Module::INPUT_G1_PORT + i));

    // IN1-IN4
    for (int i = 0; i < 4; i++)
      addInput(createInputCentered<PJ301MPort>(Vec(jINX, jUpperY[i]), module, ER301Module::INPUT_IN1_PORT + i));

    // OUT1-OUT4
    for (int i = 0; i < 4; i++)
      addOutput(createOutputCentered<PJ301MPort>(Vec(jOUTX, jUpperY[i]), module, ER301Module::OUTPUT_OUT1 + i));

    // ABCD CV inputs (4 columns × 3 rows)
    int cvInputIds[4][3] = {
        {ER301Module::INPUT_A1_PORT, ER301Module::INPUT_A2_PORT, ER301Module::INPUT_A3_PORT},
        {ER301Module::INPUT_B1_PORT, ER301Module::INPUT_B2_PORT, ER301Module::INPUT_B3_PORT},
        {ER301Module::INPUT_C1_PORT, ER301Module::INPUT_C2_PORT, ER301Module::INPUT_C3_PORT},
        {ER301Module::INPUT_D1_PORT, ER301Module::INPUT_D2_PORT, ER301Module::INPUT_D3_PORT}};
    for (int col = 0; col < 4; col++)
      for (int row = 0; row < 3; row++)
        addInput(createInputCentered<PJ301MPort>(Vec(cvJx[col], jABCDY[row]), module, cvInputIds[col][row]));
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

    // ── Panel background from PNG image ──
    if (panelImage < 0)
    {
      std::string imgPath = rack::asset::plugin(pluginInstance, "res/Panel.png");
      panelImage = nvgCreateImage(vg, imgPath.c_str(), 0);
    }
    if (panelImage >= 0)
    {
      NVGpaint bgPaint = nvgImagePattern(vg, 0, 0, pw, ph, 0, panelImage, 1.0f);
      nvgBeginPath(vg);
      nvgRect(vg, 0, 0, pw, ph);
      nvgFillPaint(vg, bgPaint);
      nvgFill(vg);
    }
    else
    {
      // Fallback: solid grey if image failed to load
      nvgBeginPath(vg);
      nvgRect(vg, 0, 0, pw, ph);
      nvgFillColor(vg, nvgRGB(205, 206, 206));
      nvgFill(vg);
    }

    // ── Main Display (render pixel buffer on top of PNG display area) ──
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
        NVGpaint paint = nvgImagePattern(vg, mainDispX, mainDispY, mainDispW, mainDispH, 0, mainImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, mainDispX, mainDispY, mainDispW, mainDispH);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
      }
    }

    // ── Sub Display ──
    if (dispBuf)
    {
      decodeSubDisplay(dispBuf);
      if (subImage < 0)
        subImage = nvgCreateImageRGBA(vg, SUB_HORIZONTAL_PIXELS, SUB_VERTICAL_PIXELS, 0, subPixels);
      else
        nvgUpdateImage(vg, subImage, subPixels);
      if (subImage >= 0)
      {
        NVGpaint paint = nvgImagePattern(vg, subDispX, subDispY, subDispW, subDispH, 0, subImage, 1.0f);
        nvgBeginPath(vg);
        nvgRect(vg, subDispX, subDispY, subDispW, subDispH);
        nvgFillPaint(vg, paint);
        nvgFill(vg);
      }
    }

    ModuleWidget::draw(args);
  }
};

Model *modelER301 = createModel<ER301Module, ER301Widget>("ER301");
