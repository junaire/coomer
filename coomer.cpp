#include <GL/glx.h>
#include <GLES3/gl3.h>

#include <X11/X.h>
#include <X11/extensions/Xrandr.h>
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

constexpr auto INITIAL_FL_DELTA_RADIUS = 250.0;
constexpr auto FL_DELTA_RADIUS_DECELERATION = 10.0;
constexpr auto VELOCITY_THRESHOLD = 15.0;

struct Vec2f {
  float x = 0.0f;
  float y = 0.0f;

  Vec2f() = default;
  Vec2f(float x, float y) : x(x), y(y) {}

  Vec2f operator*(float s) const { return Vec2f(x * s, y * s); }

  Vec2f operator/(float s) const { return Vec2f(x / s, y / s); }

  Vec2f operator*(const Vec2f &b) const { return Vec2f(x * b.x, y * b.y); }

  Vec2f operator/(const Vec2f &b) const { return Vec2f(x / b.x, y / b.y); }

  Vec2f operator-(const Vec2f &b) const { return Vec2f(x - b.x, y - b.y); }

  Vec2f operator+(const Vec2f &b) const { return Vec2f(x + b.x, y + b.y); }

  Vec2f &operator+=(const Vec2f &b) {
    x += b.x;
    y += b.y;
    return *this;
  }

  Vec2f &operator-=(const Vec2f &b) {
    x -= b.x;
    y -= b.y;
    return *this;
  }

  float length() { return std::sqrt(x * x + y * y); }

  Vec2f normalize() {
    float len = length();
    if (len == 0.0f) {
      return Vec2f();
    }
    return Vec2f(x / len, y / len);
  }
};

struct Config {
  float min_scale = 0.01;
  float scroll_speed = 1.5;
  float drag_friction = 6.0;
  float scale_friction = 4.0;

  Config() = default;
  static Config load(std::string filepath) {
    Config config;
    // Read the file from filepath.
    // foreach lines.
    // split by '=', .....
    return config;
  }
};

struct Mouse {
  Vec2f curr;
  Vec2f prev;
  bool drag;
};

struct Camera {
  Vec2f position;
  Vec2f velocity;
  Vec2f scale_pivot;
  float scale;
  float delta_scale;

  Vec2f world(Vec2f v) { return v / scale; }

  void update(Config &config, float dt, Mouse &mouse, XImage *image,
              Vec2f window_size) {
    if (std::abs(delta_scale) > 0.5f) {
      auto p0 = (scale_pivot - (window_size * 0.5)) / scale;
      scale = std::max(scale + delta_scale * dt, config.min_scale);

      auto p1 = (scale_pivot - (window_size * 0.5)) / scale;
      position += p0 - p1;

      delta_scale -= delta_scale * dt * config.scale_friction;
    }

    if (!mouse.drag and (velocity.length() > VELOCITY_THRESHOLD)) {
      position += velocity * dt;
      velocity -= velocity * dt * config.drag_friction;
    }
  }
};

struct FlashLight {
  bool is_enabled;
  float shadow;
  float radius;
  float delta_radius;

  void update(float dt) {
    if (std::abs(delta_radius) > 1.0) {
      radius = std::max(0.0f, radius + delta_radius * dt);
      delta_radius -= delta_radius * FL_DELTA_RADIUS_DECELERATION * dt;
    }
    if (is_enabled) {
      shadow = std::min(shadow + 6.0 * dt, 0.8);
    } else {
      shadow = std::max(shadow - 6.0 * dt, 0.0);
    }
  }
};

struct Screenshot {
  XImage *image;
  Display *display;
  Window window;

  Screenshot(Display *display, Window window)
      : display(display), window(window) {
    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);

    image = XGetImage(display, window, 0, 0, attributes.width,
                      attributes.height, AllPlanes, ZPixmap);
  }

  void refresh() {
    XWindowAttributes attributes;
    XGetWindowAttributes(display, window, &attributes);

    XImage *refreshed =
        XGetSubImage(display, window, 0, 0, image->width, image->height,
                     AllPlanes, ZPixmap, image, 0, 0);
    if (refreshed == nullptr || refreshed->width != attributes.width ||
        refreshed->height != attributes.height) {
      XImage *new_image = XGetImage(display, window, 0, 0, attributes.width,
                                    attributes.height, AllPlanes, ZPixmap);
      if (new_image != nullptr) {
        XDestroyImage(image);
        image = new_image;
      }
    } else {
      image = refreshed;
    }
  }

  void draw(Camera *camera, GLuint shader, GLuint vao, GLuint texture,
            Vec2f window_size, Mouse &mouse, FlashLight &flash_light) {
    glClearColor(0.1, 0.1, 0.1, 1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(shader);

    glUniform2f(glGetUniformLocation(shader, "cameraPos"), camera->position.x,
                camera->position.y);
    glUniform1f(glGetUniformLocation(shader, "cameraScale"), camera->scale);
    glUniform2f(glGetUniformLocation(shader, "screenshotSize"), image->width,
                image->height);
    glUniform2f(glGetUniformLocation(shader, "windowSize"), window_size.x,
                window_size.y);
    glUniform2f(glGetUniformLocation(shader, "cursorPos"), mouse.curr.x,
                mouse.curr.y);
    glUniform1f(glGetUniformLocation(shader, "flShadow"), flash_light.shadow);
    glUniform1f(glGetUniformLocation(shader, "flRadius"), flash_light.radius);
    glBindVertexArray(vao);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
  }

  void saveToFile(std::string filepath) {
    FILE *f = fopen(filepath.c_str(), "wb");
    if (!f) {
      perror("File open error");
      return;
    }
    fprintf(f, "P6\n%d %d\n255\n", image->width, image->height);
    for (int i = 0; i < image->width * image->height; i++) {
      fputc(image->data[i * 4 + 2], f); // R
      fputc(image->data[i * 4 + 1], f); // G
      fputc(image->data[i * 4 + 0], f); // B
    }
    fclose(f);
  }

  ~Screenshot() {
    XDestroyImage(image);
    image = nullptr;
  }
};

Vec2f getCursorPosition(Display *display) {
  Window root, child;
  int root_x, root_y, win_x, win_y;
  unsigned mask;
  XQueryPointer(display, DefaultRootWindow(display), &root, &child, &root_x,
                &root_y, &win_x, &win_y, &mask);
  return Vec2f((float)root_x, (float)root_y);
}

std::string fragment = R"(
#version 130
out mediump vec4 color;
in mediump vec2 texcoord;
uniform sampler2D tex;
uniform vec2 cursorPos;
uniform vec2 windowSize;
uniform float flShadow;
uniform float flRadius;
uniform float cameraScale;

void main()
{
    vec4 cursor = vec4(cursorPos.x, windowSize.y - cursorPos.y, 0.0, 1.0);
    color = mix(
        texture(tex, texcoord), vec4(0.0, 0.0, 0.0, 0.0),
        length(cursor - gl_FragCoord) < (flRadius * cameraScale) ? 0.0 : flShadow);
}
)";

std::string vertex = R"(
#version 130
in vec3 aPos;
in vec2 aTexCoord;
out vec2 texcoord;

uniform vec2 cameraPos;
uniform float cameraScale;
uniform vec2 windowSize;
uniform vec2 screenshotSize;
uniform vec2 cursorPos;

vec3 to_world(vec3 v) {
    vec2 ratio = vec2(
        windowSize.x / screenshotSize.x / cameraScale,
        windowSize.y / screenshotSize.y / cameraScale);
    return vec3((v.x / screenshotSize.x * 2.0 - 1.0) / ratio.x,
                (v.y / screenshotSize.y * 2.0 - 1.0) / ratio.y,
                v.z);
}

void main()
{
  gl_Position = vec4(to_world((aPos - vec3(cameraPos * vec2(1.0, -1.0), 0.0))), 1.0);
  texcoord = aTexCoord;
}
)";

static GLuint newShader(const std::string &shader, GLenum kind) {
  GLuint result = glCreateShader(kind);

  const char *vertex_src[] = {shader.c_str()};
  glShaderSource(result, 1, vertex_src, nullptr);
  glCompileShader(result);

  GLint success;
  glGetShaderiv(result, GL_COMPILE_STATUS, &success);

  if (!success) {
    char info[512];
    glGetShaderInfoLog(result, 512, nullptr, info);
    std::cout << "Fail to compile shader\n" << info << "\n";
    exit(1);
  }

  return result;
}

static GLuint newShaderProgram() {
  GLuint result = glCreateProgram();
  GLuint vertex_shader = newShader(vertex, GL_VERTEX_SHADER);
  GLuint fragment_shader = newShader(fragment, GL_FRAGMENT_SHADER);

  glAttachShader(result, vertex_shader);
  glAttachShader(result, fragment_shader);

  glLinkProgram(result);

  glDeleteShader(vertex_shader);
  glDeleteShader(fragment_shader);

  GLint success;
  glGetProgramiv(result, GL_LINK_STATUS, &success);
  if (!success) {
    char info[512];
    glGetProgramInfoLog(result, 512, nullptr, info);
    std::cout << "Fail to create shader program:\n" << info << "\n";
    exit(1);
  }

  glUseProgram(result);

  return result;
}

int errorHandler(Display *display, XErrorEvent *err) {
  char msg[256];
  XGetErrorText(display, err->error_code, msg, 256);
  std::cout << "X11 error:" << msg << "\n";
  return 0;
}

int main() {
  Config config;

  bool windowed = false;

  Display *display = XOpenDisplay(nullptr);
  assert(display != nullptr);
  // set error handler.
  XSetErrorHandler(errorHandler);

  auto tracking_window = DefaultRootWindow(display);

  XRRScreenConfiguration *screen_config =
      XRRGetScreenInfo(display, DefaultRootWindow(display));

  auto rate = XRRConfigCurrentRate(screen_config);
  std::cout << "rate: " << rate << "\n";

  auto screen = XDefaultScreen(display);

  // check glx version.
  int glx_maj, glx_min;
  if (!glXQueryVersion(display, &glx_maj, &glx_min) ||
      (glx_maj == 1 && glx_min < 3) || glx_maj < 1) {
    std::cout << "Invalid GLX version\n";
  }

  std::cout << "GLX version:" << glx_maj << "." << glx_min << "\n";
  std::cout << "GLX extension: " << glXQueryExtensionsString(display, screen)
            << "\n";

  int attrs[] = {GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, 0};

  XVisualInfo *vi = glXChooseVisual(display, 0, attrs);
  if (vi == nullptr) {
    std::cout << "No appropriate visual found\n";
    exit(1);
  }

  std::cout << "vi " << vi->visualid << " selected\n";

  XSetWindowAttributes swa;
  swa.colormap = XCreateColormap(display, DefaultRootWindow(display),
                                 vi->visual, AllocNone);
  swa.event_mask = ButtonPressMask | ButtonReleaseMask | KeyPressMask |
                   KeyReleaseMask | PointerMotionMask | ExposureMask |
                   ClientMessage;

  if (!windowed) {
    swa.override_redirect = 1;
    swa.save_under = 1;
  }

  XWindowAttributes attributes;

  XGetWindowAttributes(display, DefaultRootWindow(display), &attributes);
  auto win = XCreateWindow(
      display, DefaultRootWindow(display), 0, 0, attributes.width,
      attributes.height, 0, vi->depth, InputOutput, vi->visual,
      CWColormap | CWEventMask | CWOverrideRedirect | CWSaveUnder, &swa);
  XMapWindow(display, win);

  XClassHint hints = {.res_name = (char *)"coomer",
                      .res_class = (char *)"Coomer"};
  XStoreName(display, win, "coomer");
  XSetClassHint(display, win, &hints);

  auto wm_delete_message = XInternAtom(display, "WM_DELETE_WINDOW", 0);

  XSetWMProtocols(display, win, &wm_delete_message, 1);

  GLXContext glc = glXCreateContext(display, vi, nullptr, GL_TRUE);

  glXMakeCurrent(display, win, glc);

  auto shader_program = newShaderProgram();
  Screenshot screenshot(display, tracking_window);

  GLuint vao, vbo, ebo;

  GLfloat w = screenshot.image->width;
  GLfloat h = screenshot.image->height;

  GLfloat vertices[][5] = {
      {w, 0, 0.0, 1.0, 1.0}, // Top right
      {w, h, 0.0, 1.0, 0.0}, // Bottom right
      {0, h, 0.0, 0.0, 0.0}, // Bottom left
      {0, 0, 0.0, 0.0, 1.0}  // Top left
  };
  GLuint indices[] = {0, 1, 3, 1, 2, 3};

  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glGenBuffers(1, &ebo);

  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(sizeof(vertices)), &vertices,
               GL_STATIC_DRAW);

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(sizeof(indices)), &indices,
               GL_STATIC_DRAW);

  GLsizei stride = 5 * sizeof(GLfloat);

  glVertexAttribPointer(0, 3, GL_FLOAT, false, stride, nullptr);
  glEnableVertexAttribArray(0);

  glVertexAttribPointer(1, 2, GL_FLOAT, false, stride,
                        (const void *)(3 * sizeof(GLfloat)));
  glEnableVertexAttribArray(1);

  GLuint texture = 0;
  glGenTextures(1, &texture);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, screenshot.image->width,
               screenshot.image->height, 0, GL_BGRA, GL_UNSIGNED_BYTE,
               screenshot.image->data);
  glGenerateMipmap(GL_TEXTURE_2D);

  glUniform1i(glGetUniformLocation(shader_program, "tex"), 0);

  glEnable(GL_TEXTURE_2D);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  bool quitting = false;

  Camera camera{.scale = 1.0};
  Vec2f pos = getCursorPosition(display);
  Mouse mouse{.curr = pos, .prev = pos};

  FlashLight flash_light{.is_enabled = false, .radius = 200.0f};

  float dt = 1 / (float)rate;
  Window origin_window;
  int revert_to_return;
  XGetInputFocus(display, &origin_window, &revert_to_return);

  while (!quitting) {
    if (!windowed) {
      XSetInputFocus(display, win, RevertToParent, CurrentTime);
    }

    XWindowAttributes wa;
    XGetWindowAttributes(display, win, &wa);
    glViewport(0, 0, wa.width, wa.height);

    XEvent xev;
    while (XPending(display) > 0) {
      XNextEvent(display, &xev);
      auto scroll_up = [&] {
        if (((xev.xkey.state & ControlMask) > 0) && flash_light.is_enabled) {
          flash_light.delta_radius += INITIAL_FL_DELTA_RADIUS;
        } else {
          camera.delta_scale += config.scroll_speed;
          camera.scale_pivot = mouse.curr;
        }
      };
      auto scroll_down = [&] {
        if (((xev.xkey.state & ControlMask) > 0) && flash_light.is_enabled) {
          flash_light.delta_radius -= INITIAL_FL_DELTA_RADIUS;
        } else {
          camera.delta_scale -= config.scroll_speed;
          camera.scale_pivot = mouse.curr;
        }
      };
      switch (xev.type) {
      case Expose:
        break;
      case MotionNotify: {
        mouse.curr = Vec2f(xev.xmotion.x, xev.xmotion.y);
        if (mouse.drag) {
          Vec2f delta = camera.world(mouse.prev) - camera.world(mouse.curr);
          camera.position += delta;
          camera.velocity = delta * rate;
        }
        mouse.prev = mouse.curr;
        break;
      }
      case ClientMessage: {
        if (xev.xclient.data.l[0] == wm_delete_message) {
          quitting = true;
        }
        break;
      }
      case KeyPress: {
        auto key = XLookupKeysym((XKeyEvent *)&xev, 0);
        switch (key) {
        case XK_equal:
          scroll_up();
          break;
        case XK_minus:
          scroll_down();
          break;
        case XK_0:
          camera.scale = 1.0;
          camera.delta_scale = 0.0;
          camera.position = Vec2f();
          camera.velocity = Vec2f();
          break;
        case XK_f:
          flash_light.is_enabled = !flash_light.is_enabled;
          break;
        case XK_q:
        case XK_Escape:
          quitting = true;
          break;
        default:
          break;
        }
        break;
      }
      case ButtonPress: {
        switch (xev.xbutton.button) {
        case Button1:
          mouse.prev = mouse.curr;
          mouse.drag = true;
          camera.velocity = Vec2f();
          break;
        case Button4:
          scroll_up();
          break;
        case Button5:
          scroll_down();
          break;
        default:
          break;
        }
        break;
      }
      case ButtonRelease: {
        if (xev.xbutton.button == Button1) {
          mouse.drag = false;
        }
        break;
      }
      default:
        break;
      }
    }

    camera.update(config, dt, mouse, screenshot.image,
                  Vec2f(wa.width, wa.height));
    flash_light.update(dt);
    screenshot.draw(&camera, shader_program, vao, texture,
                    Vec2f(wa.width, wa.height), mouse, flash_light);
    glXSwapBuffers(display, win);
    glFinish();
  }

  XSetInputFocus(display, origin_window, RevertToParent, CurrentTime);
  XSync(display, 0);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
  glDeleteBuffers(1, &ebo);
  XCloseDisplay(display);
}
