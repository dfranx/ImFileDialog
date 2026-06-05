#include <SDL.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_sdl2.h>
#include <imgui/backends/imgui_impl_opengl3.h>
#include <apifilesystem/ghc/filesystem.hpp>
#include <apifilesystem/filesystem.hpp>
#include <ctime>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <GL/glew.h>
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#if (defined(_WIN32) && defined(_MSC_VER))
#pragma comment(lib, "opengl32.lib")
#endif

#include "ImFileDialog.h"

// SDL defines main
#undef main

static const char *fonts[158] = {
  "fonts/000-notosans-regular.ttf",
  "fonts/001-notokufiarabic-regular.ttf",
  "fonts/002-notomusic-regular.ttf",
  "fonts/003-notonaskharabic-regular.ttf",
  "fonts/004-notonaskharabicui-regular.ttf",
  "fonts/005-notonastaliqurdu-regular.ttf",
  "fonts/006-notosansadlam-regular.ttf",
  "fonts/007-notosansadlamunjoined-regular.ttf",
  "fonts/008-notosansanatolianhieroglyphs-regular.ttf",
  "fonts/009-notosansarabic-regular.ttf",
  "fonts/010-notosansarabicui-regular.ttf",
  "fonts/011-notosansarmenian-regular.ttf",
  "fonts/012-notosansavestan-regular.ttf",
  "fonts/013-notosansbamum-regular.ttf",
  "fonts/014-notosansbassavah-regular.ttf",
  "fonts/015-notosansbatak-regular.ttf",
  "fonts/016-notosansbengali-regular.ttf",
  "fonts/017-notosansbengaliui-regular.ttf",
  "fonts/018-notosansbhaiksuki-regular.ttf",
  "fonts/019-notosansbrahmi-regular.ttf",
  "fonts/020-notosansbuginese-regular.ttf",
  "fonts/021-notosansbuhid-regular.ttf",
  "fonts/022-notosanscanadianaboriginal-regular.ttf",
  "fonts/023-notosanscarian-regular.ttf",
  "fonts/024-notosanscaucasianalbanian-regular.ttf",
  "fonts/025-notosanschakma-regular.ttf",
  "fonts/026-notosanscham-regular.ttf",
  "fonts/027-notosanscherokee-regular.ttf",
  "fonts/028-notosanscoptic-regular.ttf",
  "fonts/029-notosanscuneiform-regular.ttf",
  "fonts/030-notosanscypriot-regular.ttf",
  "fonts/031-notosansdeseret-regular.ttf",
  "fonts/032-notosansdevanagari-regular.ttf",
  "fonts/033-notosansdevanagariui-regular.ttf",
  "fonts/034-notosansdisplay-regular.ttf",
  "fonts/035-notosansduployan-regular.ttf",
  "fonts/036-notosansegyptianhieroglyphs-regular.ttf",
  "fonts/037-notosanselbasan-regular.ttf",
  "fonts/038-notosansethiopic-regular.ttf",
  "fonts/039-notosansgeorgian-regular.ttf",
  "fonts/040-notosansglagolitic-regular.ttf",
  "fonts/041-notosansgothic-regular.ttf",
  "fonts/042-notosansgrantha-regular.ttf",
  "fonts/043-notosansgujarati-regular.ttf",
  "fonts/044-notosansgujaratiui-regular.ttf",
  "fonts/045-notosansgurmukhi-regular.ttf",
  "fonts/046-notosansgurmukhiui-regular.ttf",
  "fonts/047-notosanshanifirohingya-regular.ttf",
  "fonts/048-notosanshanunoo-regular.ttf",
  "fonts/049-notosanshatran-regular.ttf",
  "fonts/050-notosanshebrew-regular.ttf",
  "fonts/051-notosansimperialaramaic-regular.ttf",
  "fonts/052-notosansindicsiyaqnumbers-regular.ttf",
  "fonts/053-notosansinscriptionalpahlavi-regular.ttf",
  "fonts/054-notosansinscriptionalparthian-regular.ttf",
  "fonts/055-notosansjavanese-regular.ttf",
  "fonts/056-notosanskaithi-regular.ttf",
  "fonts/057-notosanskannada-regular.ttf",
  "fonts/058-notosanskannadaui-regular.ttf",
  "fonts/059-notosanskayahli-regular.ttf",
  "fonts/060-notosanskharoshthi-regular.ttf",
  "fonts/061-notosanskhmer-regular.ttf",
  "fonts/062-notosanskhmerui-regular.ttf",
  "fonts/063-notosanskhojki-regular.ttf",
  "fonts/064-notosanskhudawadi-regular.ttf",
  "fonts/065-notosanslao-regular.ttf",
  "fonts/066-notosanslaoui-regular.ttf",
  "fonts/067-notosanslepcha-regular.ttf",
  "fonts/068-notosanslimbu-regular.ttf",
  "fonts/069-notosanslineara-regular.ttf",
  "fonts/070-notosanslinearb-regular.ttf",
  "fonts/071-notosanslisu-regular.ttf",
  "fonts/072-notosanslycian-regular.ttf",
  "fonts/073-notosanslydian-regular.ttf",
  "fonts/074-notosansmahajani-regular.ttf",
  "fonts/075-notosansmalayalam-regular.ttf",
  "fonts/076-notosansmalayalamui-regular.ttf",
  "fonts/077-notosansmandaic-regular.ttf",
  "fonts/078-notosansmanichaean-regular.ttf",
  "fonts/079-notosansmarchen-regular.ttf",
  "fonts/080-notosansmath-regular.ttf",
  "fonts/081-notosansmayannumerals-regular.ttf",
  "fonts/082-notosansmeeteimayek-regular.ttf",
  "fonts/083-notosansmendekikakui-regular.ttf",
  "fonts/084-notosansmeroitic-regular.ttf",
  "fonts/085-notosansmiao-regular.ttf",
  "fonts/086-notosansmodi-regular.ttf",
  "fonts/087-notosansmongolian-regular.ttf",
  "fonts/088-notosansmono-regular.ttf",
  "fonts/089-notosansmro-regular.ttf",
  "fonts/090-notosansmultani-regular.ttf",
  "fonts/091-notosansmyanmar-regular.ttf",
  "fonts/092-notosansmyanmarui-regular.ttf",
  "fonts/093-notosansnabataean-regular.ttf",
  "fonts/094-notosansnewa-regular.ttf",
  "fonts/095-notosansnewtailue-regular.ttf",
  "fonts/096-notosansnko-regular.ttf",
  "fonts/097-notosansogham-regular.ttf",
  "fonts/098-notosansolchiki-regular.ttf",
  "fonts/099-notosansoldhungarian-regular.ttf",
  "fonts/100-notosansolditalic-regular.ttf",
  "fonts/101-notosansoldnortharabian-regular.ttf",
  "fonts/102-notosansoldpermic-regular.ttf",
  "fonts/103-notosansoldpersian-regular.ttf",
  "fonts/104-notosansoldsogdian-regular.ttf",
  "fonts/105-notosansoldsoutharabian-regular.ttf",
  "fonts/106-notosansoldturkic-regular.ttf",
  "fonts/107-notosansoriya-regular.ttf",
  "fonts/108-notosansoriyaui-regular.ttf",
  "fonts/109-notosansosage-regular.ttf",
  "fonts/110-notosansosmanya-regular.ttf",
  "fonts/111-notosanspahawhhmong-regular.ttf",
  "fonts/112-notosanspalmyrene-regular.ttf",
  "fonts/113-notosanspaucinhau-regular.ttf",
  "fonts/114-notosansphagspa-regular.ttf",
  "fonts/115-notosansphoenician-regular.ttf",
  "fonts/116-notosanspsalterpahlavi-regular.ttf",
  "fonts/117-notosansrejang-regular.ttf",
  "fonts/118-notosansrunic-regular.ttf",
  "fonts/119-notosanssamaritan-regular.ttf",
  "fonts/120-notosanssaurashtra-regular.ttf",
  "fonts/121-notosanssharada-regular.ttf",
  "fonts/122-notosansshavian-regular.ttf",
  "fonts/123-notosanssiddham-regular.ttf",
  "fonts/124-notosanssinhala-regular.ttf",
  "fonts/125-notosanssinhalaui-regular.ttf",
  "fonts/126-notosanssorasompeng-regular.ttf",
  "fonts/127-notosanssundanese-regular.ttf",
  "fonts/128-notosanssylotinagri-regular.ttf",
  "fonts/129-notosanssymbols2-regular.ttf",
  "fonts/130-notosanssymbols-regular.ttf",
  "fonts/131-notosanssyriac-regular.ttf",
  "fonts/132-notosanstagalog-regular.ttf",
  "fonts/133-notosanstagbanwa-regular.ttf",
  "fonts/134-notosanstaile-regular.ttf",
  "fonts/135-notosanstaitham-regular.ttf",
  "fonts/136-notosanstaiviet-regular.ttf",
  "fonts/137-notosanstakri-regular.ttf",
  "fonts/138-notosanstamil-regular.ttf",
  "fonts/139-notosanstamilsupplement-regular.ttf",
  "fonts/140-notosanstamilui-regular.ttf",
  "fonts/141-notosanstelugu-regular.ttf",
  "fonts/142-notosansteluguui-regular.ttf",
  "fonts/143-notosansthaana-regular.ttf",
  "fonts/144-notosansthai-regular.ttf",
  "fonts/145-notosansthaiui-regular.ttf",
  "fonts/146-notosanstibetan-regular.ttf",
  "fonts/147-notosanstifinagh-regular.ttf",
  "fonts/148-notosanstirhuta-regular.ttf",
  "fonts/149-notosansugaritic-regular.ttf",
  "fonts/150-notosansvai-regular.ttf",
  "fonts/151-notosanswarangciti-regular.ttf",
  "fonts/152-notosansyi-regular.ttf",
  "fonts/153-notosanstc-regular.otf",
  "fonts/154-notosansjp-regular.otf",
  "fonts/155-notosanskr-regular.otf",
  "fonts/156-notosanssc-regular.otf",
  "fonts/157-notosanshk-regular.otf"
};

int main(int argc, char *argv[]) {
  bool run = true;
  srand(time(nullptr));
  #if defined(_WIN32)
  SetConsoleOutputCP(CP_UTF8);
  #endif

  // init sdl2
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) < 0) {
    printf("Failed to initialize SDL2\n");
    return 0;
  }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

  // create the window
  int wndWidth = 1024, wndHeight = 768;
  SDL_Window *wnd = SDL_CreateWindow("File Dialog", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, wndWidth, wndHeight, windowFlags);
  SDL_MaximizeWindow(wnd);
  
  // create the GL context
  SDL_GLContext glContext = SDL_GL_CreateContext(wnd);
  SDL_GL_MakeCurrent(wnd, glContext);
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_STENCIL_TEST);

  // init glew
  glewExperimental = true;
  if (glewInit() != GLEW_OK) {
    printf("Failed to initialize GLEW\n");
    return 0;
  }

  // imgui
  static std::string ini;
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO(); (void)io;
  #if defined(_WIN32)
  if (ngs::fs::environment_get_variable("IMGUI_CONFIG_HOME").empty())
    ngs::fs::environment_set_variable("IMGUI_CONFIG_HOME", ngs::fs::environment_get_variable("USERPROFILE"));
  #else
  if (ngs::fs::environment_get_variable("IMGUI_CONFIG_HOME").empty())
    ngs::fs::environment_set_variable("IMGUI_CONFIG_HOME", ngs::fs::environment_get_variable("HOME"));
  #endif
  if (ngs::fs::environment_get_variable("IMGUI_CONFIG_FOLDER").empty())
    ngs::fs::environment_set_variable("IMGUI_CONFIG_FOLDER", "filedialogs");
  if (ngs::fs::environment_get_variable("IMGUI_CONFIG_FAVS").empty())
    ngs::fs::environment_set_variable("IMGUI_CONFIG_FAVS", "filedialogs-favs.txt");
  if (ngs::fs::environment_get_variable("IMGUI_CONFIG_INI").empty())
    ngs::fs::environment_set_variable("IMGUI_CONFIG_INI", "filedialogs-conf.ini");
  #if defined(_WIN32)
  std::string config_path = ngs::fs::environment_expand_variables("${IMGUI_CONFIG_HOME}\\.config\\${IMGUI_CONFIG_FOLDER}");
  if (!config_path.empty()) {
    if (!ngs::fs::directory_exists(config_path)) ngs::fs::directory_create(config_path);
    ini = ngs::fs::environment_expand_variables(config_path + "\\${IMGUI_CONFIG_INI}");
    int attr = GetFileAttributesW(ghc::filesystem::path(config_path).wstring().c_str());
    if ((attr & FILE_ATTRIBUTE_HIDDEN) == 0) SetFileAttributesW(ghc::filesystem::path(config_path).wstring().c_str(), attr | FILE_ATTRIBUTE_HIDDEN);
  }
  #else
  std::string config_path = ngs::fs::environment_expand_variables("${IMGUI_CONFIG_HOME}/.config/${IMGUI_CONFIG_FOLDER}");
  if (!config_path.empty()) {
    if (!ngs::fs::directory_exists(config_path)) ngs::fs::directory_create(config_path);
    ini = ngs::fs::environment_expand_variables(config_path + "/${IMGUI_CONFIG_INI}");
  }
  #endif
  io.IniFilename = ((!ini.empty()) ? ini.c_str() : nullptr);
  
  ImFontConfig config;
  config.MergeMode = true; 
  ImFont *font = nullptr;
  ImWchar ranges[] = { 0x0020, 0xFFFF, 0 };
  for (unsigned i = 0; i < 158; i++)
    if (ngs::fs::file_exists(ngs::fs::executable_get_directory() + std::string(fonts[i])))
      io.Fonts->AddFontFromFileTTF((ngs::fs::executable_get_directory() + std::string(fonts[i])).c_str(), 20, (!i) ? nullptr : &config, ranges);

  ImGui::StyleColorsLight();
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowBorderSize = 0.0;
  style.WindowPadding = ImVec2(15, 15);
  style.WindowRounding = 0.0f;
  style.FramePadding = ImVec2(5, 5);
  style.FrameRounding = 4.0f;
  style.ItemSpacing = ImVec2(12, 8);
  style.ItemInnerSpacing = ImVec2(8, 6);
  style.IndentSpacing = 25.0f;
  style.ScrollbarSize = 15.0f;
  style.ScrollbarRounding = 9.0f;
  style.GrabMinSize = 5.0f;
  style.GrabRounding = 3.0f;

  ImGui_ImplSDL2_InitForOpenGL(wnd, glContext);
  ImGui_ImplOpenGL3_Init();

  // ImFileDialog requires you to set the CreateTexture and DeleteTexture
  ifd::FileDialog::Instance().CreateTexture = [](uint8_t *data, int w, int h, char fmt) -> void * {
    GLuint tex;

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, (fmt == 0) ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);

    return (void *)(uintptr_t)tex;
  };
  ifd::FileDialog::Instance().DeleteTexture = [](void *tex) {
    GLuint texID = (GLuint)(uintptr_t)tex;
    glDeleteTextures(1, &texID);
  };

  SDL_Event event;
  while (run) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT)
        run = false;
      else if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_MOVED || event.window.event == SDL_WINDOWEVENT_MAXIMIZED || event.window.event == SDL_WINDOWEVENT_RESIZED) {
          Uint32 wndFlags = SDL_GetWindowFlags(wnd);
          SDL_GetWindowSize(wnd, &wndWidth, &wndHeight);
        }
      }

      // let ImGui handle the events
      ImGui_ImplSDL2_ProcessEvent(&event);
    }

    if (!run) break;

    // Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    // Simple window
    ImGui::Begin("Control Panel");
    if (ImGui::Button("Open File"))
      ifd::FileDialog::Instance().Open("ShaderOpenDialog", "Open a Shader", "Image Files (*.png *.jpg *.jpeg *.bmp *.tga){.png,.jpg,.jpeg,.bmp,.tga},.*", true);
    if (ImGui::Button("Open Directory"))
      ifd::FileDialog::Instance().Open("DirectoryOpenDialog", "Open a Directory", "");
    if (ImGui::Button("Save File"))
      ifd::FileDialog::Instance().Save("ShaderSaveDialog", "Save a Shader", "Shader Files (*.sprj){.sprj},.*");
    ImGui::End();

    // file dialogs
    if (ifd::FileDialog::Instance().IsDone("ShaderOpenDialog")) {
      if (ifd::FileDialog::Instance().HasResult()) {
        const std::vector<ghc::filesystem::path>& res = ifd::FileDialog::Instance().GetResults();
        for (const auto& r : res) // ShaderOpenDialog supports multiselection
          printf("OPEN[%s]\n", r.u8string().c_str());
      }
      ifd::FileDialog::Instance().Close();
    }
    if (ifd::FileDialog::Instance().IsDone("DirectoryOpenDialog")) {
      if (ifd::FileDialog::Instance().HasResult()) {
        std::string res = ifd::FileDialog::Instance().GetResult().u8string();
        printf("DIRECTORY[%s]\n", res.c_str());
      }
      ifd::FileDialog::Instance().Close();
    }
    if (ifd::FileDialog::Instance().IsDone("ShaderSaveDialog")) {
      if (ifd::FileDialog::Instance().HasResult()) {
        std::string res = ifd::FileDialog::Instance().GetResult().u8string();
        printf("SAVE[%s]\n", res.c_str());
      }
      ifd::FileDialog::Instance().Close();
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glViewport(0, 0, wndWidth, wndHeight);

    // render Dear ImGui
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    SDL_GL_SwapWindow(wnd);
  }

  // Dear ImGui cleanup
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  // SDL2 cleanup
  SDL_GL_DeleteContext(glContext);
  SDL_DestroyWindow(wnd);
  SDL_Quit();

  return 0;
}
