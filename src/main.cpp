#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int   WIN_W          = 800;
static constexpr int   WIN_H          = 600;
static constexpr int   CHUNK_SIZE     = 16;
static constexpr int   MAX_FRAMES     = 2;

#ifdef NDEBUG
static constexpr bool  ENABLE_VALID   = false;
#else
static constexpr bool  ENABLE_VALID   = true;
#endif

static const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};
static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ─────────────────────────────────────────────────────────────────────────────
// UBO (matches the push-constant / uniform layout in the shader)
// ─────────────────────────────────────────────────────────────────────────────
struct UBO {
    mat4 mvp;
};

// ─────────────────────────────────────────────────────────────────────────────
// Camera
// ─────────────────────────────────────────────────────────────────────────────
struct Camera {
    vec3  position;
    float yaw;
    float pitch;
    float speed;
    float sensitivity;
};

static Camera camera      = {{0.f, 0.f, 50.f}, -90.f, 0.f, 10.f, 0.1f};
static float  lastX       = WIN_W / 2.f;
static float  lastY       = WIN_H / 2.f;
static bool   firstMouse  = true;
static float  deltaTime   = 0.f;
static float  lastFrame   = 0.f;
static bool   wireframe   = false;
static bool   ePrevious   = false;

// ─────────────────────────────────────────────────────────────────────────────
// Cube face vertices  (+X -X +Y -Y +Z -Z)
// ─────────────────────────────────────────────────────────────────────────────
static float faceVerts[6][18] = {
    { 0.5f,-0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f,-0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f, -0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f, -0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f,-0.5f,  0.5f,-0.5f, 0.5f, -0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f, 0.5f,  0.5f,-0.5f, 0.5f,  0.5f, 0.5f, 0.5f,  0.5f, 0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f,-0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f, -0.5f,-0.5f,-0.5f, -0.5f, 0.5f,-0.5f, -0.5f, 0.5f,-0.5f,  0.5f, 0.5f,-0.5f,  0.5f,-0.5f,-0.5f}
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("Cannot open file: " + path);
    size_t size = (size_t)f.tellg();
    std::vector<char> buf(size);
    f.seekg(0); f.read(buf.data(), size);
    return buf;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* pData,
    void*)
{
    std::cerr << "[Vulkan] " << pData->pMessage << "\n";
    return VK_FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Application class
// ─────────────────────────────────────────────────────────────────────────────
class App {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    // ── window ────────────────────────────────────────────────────────────
    GLFWwindow* window = nullptr;

    // ── core Vulkan handles ───────────────────────────────────────────────
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         physDev        = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;

    // ── swap chain ────────────────────────────────────────────────────────
    VkSwapchainKHR             swapchain      = VK_NULL_HANDLE;
    std::vector<VkImage>       scImages;
    std::vector<VkImageView>   scViews;
    VkFormat                   scFormat{};
    VkExtent2D                 scExtent{};

    // ── render pass & pipeline ────────────────────────────────────────────
    VkRenderPass               renderPass     = VK_NULL_HANDLE;
    VkDescriptorSetLayout      descLayout     = VK_NULL_HANDLE;
    VkPipelineLayout           pipeLayout     = VK_NULL_HANDLE;
    VkPipeline                 pipeline       = VK_NULL_HANDLE;
    VkPipeline                 pipelineWire   = VK_NULL_HANDLE;

    // ── framebuffers & depth ──────────────────────────────────────────────
    std::vector<VkFramebuffer> framebuffers;
    VkImage                    depthImage     = VK_NULL_HANDLE;
    VkDeviceMemory             depthMemory    = VK_NULL_HANDLE;
    VkImageView                depthView      = VK_NULL_HANDLE;

    // ── vertex buffer ─────────────────────────────────────────────────────
    VkBuffer                   vertexBuffer   = VK_NULL_HANDLE;
    VkDeviceMemory             vertexMemory   = VK_NULL_HANDLE;
    uint32_t                   vertexCount    = 0;

    // ── uniform buffers (one per frame in flight) ─────────────────────────
    std::vector<VkBuffer>       ubos;
    std::vector<VkDeviceMemory> uboMems;
    std::vector<void*>          uboMapped;

    // ── descriptors ───────────────────────────────────────────────────────
    VkDescriptorPool             descPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descSets;

    // ── commands & sync ───────────────────────────────────────────────────
    VkCommandPool                cmdPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBufs;
    std::vector<VkSemaphore>     imgAvail;
    std::vector<VkSemaphore>     renderDone;
    std::vector<VkFence>         inFlight;
    uint32_t                     currentFrame = 0;

    bool framebufferResized = false;
    uint32_t imgAvailIdx = 0; // cycles through imgAvail per swapchain image count

    // ─────────────────────────────────────────────────────────────────────
    // Window
    // ─────────────────────────────────────────────────────────────────────
    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        window = glfwCreateWindow(WIN_W, WIN_H, "Minecraft in Vulkan", nullptr, nullptr);
        glfwSetWindowUserPointer(window, this);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int, int){
            static_cast<App*>(glfwGetWindowUserPointer(w))->framebufferResized = true;
        });
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwSetCursorPosCallback(window, [](GLFWwindow*, double x, double y){
            if (firstMouse) { lastX=(float)x; lastY=(float)y; firstMouse=false; }
            float xo=(float)(x-lastX), yo=(float)(lastY-y);
            lastX=(float)x; lastY=(float)y;
            camera.yaw   += xo * camera.sensitivity;
            camera.pitch += yo * camera.sensitivity;
            if (camera.pitch >  89.f) camera.pitch =  89.f;
            if (camera.pitch < -89.f) camera.pitch = -89.f;
        });
    }

    // ─────────────────────────────────────────────────────────────────────
    // Input
    // ─────────────────────────────────────────────────────────────────────
    void processInput() {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);

        float v = camera.speed * deltaTime;
        vec3 front = {
            cosf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch)),
            sinf(glm_rad(camera.pitch)),
            sinf(glm_rad(camera.yaw))  * cosf(glm_rad(camera.pitch))
        };
        glm_vec3_normalize(front);
        vec3 worldUp = {0,1,0};
        vec3 right; glm_vec3_cross(front, worldUp, right); glm_vec3_normalize(right);
        vec3 up;    glm_vec3_cross(right, front, up);

        auto move = [&](vec3 dir, float sign){
            vec3 tmp; glm_vec3_scale(dir, sign * v, tmp);
            glm_vec3_add(camera.position, tmp, camera.position);
        };
        if (glfwGetKey(window, GLFW_KEY_W)            == GLFW_PRESS) move(front,  1);
        if (glfwGetKey(window, GLFW_KEY_S)            == GLFW_PRESS) move(front, -1);
        if (glfwGetKey(window, GLFW_KEY_A)            == GLFW_PRESS) move(right, -1);
        if (glfwGetKey(window, GLFW_KEY_D)            == GLFW_PRESS) move(right,  1);
        if (glfwGetKey(window, GLFW_KEY_SPACE)        == GLFW_PRESS) move(up,     1);
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) move(up,    -1);

        bool eNow = glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS;
        if (eNow && !ePrevious) wireframe = !wireframe;
        ePrevious = eNow;
    }

    // ─────────────────────────────────────────────────────────────────────
    // Vulkan init sequence
    // ─────────────────────────────────────────────────────────────────────
    void initVulkan() {
        createInstance();
        setupDebugMessenger();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createGraphicsPipelines();
        createDepthResources();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createUniformBuffers();
        createDescriptorPool();
        createDescriptorSets();
        createCommandBuffers();
        createSyncObjects();
    }

    // ── Instance ──────────────────────────────────────────────────────────
    void createInstance() {
        if (ENABLE_VALID && !checkValidationSupport())
            throw std::runtime_error("Validation layers not available");

        VkApplicationInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName   = "Minecraft";
        ai.applicationVersion = VK_MAKE_VERSION(1,0,0);
        ai.pEngineName        = "No Engine";
        ai.engineVersion      = VK_MAKE_VERSION(1,0,0);
        ai.apiVersion         = VK_API_VERSION_1_0;

        auto exts = getRequiredExtensions();

        VkInstanceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &ai;
        ci.enabledExtensionCount   = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        if (ENABLE_VALID) {
            ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
            ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }
        if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
    }

    bool checkValidationSupport() {
        uint32_t n; vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> layers(n);
        vkEnumerateInstanceLayerProperties(&n, layers.data());
        for (auto* name : VALIDATION_LAYERS) {
            bool found = false;
            for (auto& l : layers) if (strcmp(name, l.layerName)==0){ found=true; break; }
            if (!found) return false;
        }
        return true;
    }

    std::vector<const char*> getRequiredExtensions() {
        uint32_t n; const char** glfwExts = glfwGetRequiredInstanceExtensions(&n);
        std::vector<const char*> exts(glfwExts, glfwExts+n);
        if (ENABLE_VALID) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        return exts;
    }

    // ── Debug messenger ───────────────────────────────────────────────────
    void setupDebugMessenger() {
        if (!ENABLE_VALID) return;
        VkDebugUtilsMessengerCreateInfoEXT ci{};
        ci.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        ci.messageType     = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT    |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        ci.pfnUserCallback = debugCallback;

        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)
            vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT");
        if (!fn || fn(instance, &ci, nullptr, &debugMessenger) != VK_SUCCESS)
            std::cerr << "Warning: Could not create debug messenger\n";
    }

    // ── Surface ───────────────────────────────────────────────────────────
    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("glfwCreateWindowSurface failed");
    }

    // ── Physical device ───────────────────────────────────────────────────
    struct QueueFamilies {
        std::optional<uint32_t> graphics, present;
        bool complete() const { return graphics.has_value() && present.has_value(); }
    };

    QueueFamilies findQueueFamilies(VkPhysicalDevice dev) {
        QueueFamilies qf;
        uint32_t n; vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, nullptr);
        std::vector<VkQueueFamilyProperties> props(n);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, props.data());
        for (uint32_t i=0; i<n; i++) {
            if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qf.graphics = i;
            VkBool32 ps=false;
            vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &ps);
            if (ps) qf.present = i;
            if (qf.complete()) break;
        }
        return qf;
    }

    bool deviceSuitable(VkPhysicalDevice dev) {
        if (!findQueueFamilies(dev).complete()) return false;
        // check swapchain extension
        uint32_t n; vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr);
        std::vector<VkExtensionProperties> exts(n);
        vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, exts.data());
        for (auto* req : DEVICE_EXTENSIONS) {
            bool found=false;
            for (auto& e : exts) if (strcmp(req, e.extensionName)==0){ found=true; break; }
            if (!found) return false;
        }
        // check swapchain adequacy
        uint32_t fmtN, pmN;
        vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmtN, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pmN, nullptr);
        return fmtN > 0 && pmN > 0;
    }

    void pickPhysicalDevice() {
        uint32_t n; vkEnumeratePhysicalDevices(instance, &n, nullptr);
        if (n==0) throw std::runtime_error("No Vulkan GPUs found");
        std::vector<VkPhysicalDevice> devs(n);
        vkEnumeratePhysicalDevices(instance, &n, devs.data());
        for (auto& d : devs) if (deviceSuitable(d)) { physDev=d; return; }
        throw std::runtime_error("No suitable GPU found");
    }

    // ── Logical device ────────────────────────────────────────────────────
    void createLogicalDevice() {
        auto qf = findQueueFamilies(physDev);
        std::set<uint32_t> uniqueQ = {qf.graphics.value(), qf.present.value()};
        float prio = 1.f;
        std::vector<VkDeviceQueueCreateInfo> qcis;
        for (uint32_t q : uniqueQ) {
            VkDeviceQueueCreateInfo ci{};
            ci.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            ci.queueFamilyIndex = q;
            ci.queueCount       = 1;
            ci.pQueuePriorities = &prio;
            qcis.push_back(ci);
        }
        VkPhysicalDeviceFeatures feat{}; 
        feat.fillModeNonSolid = VK_TRUE; // needed for wireframe

        VkDeviceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount    = (uint32_t)qcis.size();
        ci.pQueueCreateInfos       = qcis.data();
        ci.enabledExtensionCount   = (uint32_t)DEVICE_EXTENSIONS.size();
        ci.ppEnabledExtensionNames = DEVICE_EXTENSIONS.data();
        ci.pEnabledFeatures        = &feat;
        if (ENABLE_VALID) {
            ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
            ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
        }
        if (vkCreateDevice(physDev, &ci, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed");

        vkGetDeviceQueue(device, qf.graphics.value(), 0, &graphicsQueue);
        vkGetDeviceQueue(device, qf.present.value(),  0, &presentQueue);
    }

    // ── Swapchain ─────────────────────────────────────────────────────────
    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& fmts) {
        for (auto& f : fmts)
            if (f.format==VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                return f;
        return fmts[0];
    }
    VkPresentModeKHR choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
        for (auto& m : modes) if (m==VK_PRESENT_MODE_MAILBOX_KHR) return m;
        return VK_PRESENT_MODE_FIFO_KHR;
    }
    VkExtent2D chooseExtent(const VkSurfaceCapabilitiesKHR& caps) {
        if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max())
            return caps.currentExtent;
        int w,h; glfwGetFramebufferSize(window,&w,&h);
        VkExtent2D e = {(uint32_t)w,(uint32_t)h};
        e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
        e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        return e;
    }

    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);
        uint32_t n;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &n, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(n);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &n, fmts.data());
        vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &n, modes.data());

        auto fmt  = chooseSurfaceFormat(fmts);
        auto pm   = choosePresentMode(modes);
        auto ext  = chooseExtent(caps);
        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) imgCount = std::min(imgCount, caps.maxImageCount);

        VkSwapchainCreateInfoKHR ci{};
        ci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface          = surface;
        ci.minImageCount    = imgCount;
        ci.imageFormat      = fmt.format;
        ci.imageColorSpace  = fmt.colorSpace;
        ci.imageExtent      = ext;
        ci.imageArrayLayers = 1;
        ci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        auto qf = findQueueFamilies(physDev);
        uint32_t qfIndices[] = {qf.graphics.value(), qf.present.value()};
        if (qf.graphics.value() != qf.present.value()) {
            ci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices   = qfIndices;
        } else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }
        ci.preTransform   = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode    = pm;
        ci.clipped        = VK_TRUE;

        if (vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");

        vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
        scImages.resize(imgCount);
        vkGetSwapchainImagesKHR(device, swapchain, &imgCount, scImages.data());
        scFormat = fmt.format;
        scExtent = ext;
    }

    // ── Image views ───────────────────────────────────────────────────────
    VkImageView makeImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo ci{};
        ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        ci.image                           = img;
        ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
        ci.format                          = fmt;
        ci.subresourceRange.aspectMask     = aspect;
        ci.subresourceRange.baseMipLevel   = 0;
        ci.subresourceRange.levelCount     = 1;
        ci.subresourceRange.baseArrayLayer = 0;
        ci.subresourceRange.layerCount     = 1;
        VkImageView v;
        if (vkCreateImageView(device, &ci, nullptr, &v) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImageView failed");
        return v;
    }

    void createImageViews() {
        scViews.resize(scImages.size());
        for (size_t i=0; i<scImages.size(); i++)
            scViews[i] = makeImageView(scImages[i], scFormat, VK_IMAGE_ASPECT_COLOR_BIT);
    }

    // ── Render pass ───────────────────────────────────────────────────────
    void createRenderPass() {
        VkAttachmentDescription color{};
        color.format         = scFormat;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depth{};
        depth.format         = VK_FORMAT_D32_SFLOAT;
        depth.samples        = VK_SAMPLE_COUNT_1_BIT;
        depth.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub{};
        sub.pipelineBindPoint       = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount    = 1;
        sub.pColorAttachments       = &colorRef;
        sub.pDepthStencilAttachment = &depthRef;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.srcAccessMask = 0;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

        std::array<VkAttachmentDescription,2> atts = {color, depth};
        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = (uint32_t)atts.size();
        ci.pAttachments    = atts.data();
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies   = &dep;

        if (vkCreateRenderPass(device, &ci, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
    }

    // ── Descriptor set layout ─────────────────────────────────────────────
    void createDescriptorSetLayout() {
        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding         = 0;
        uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo ci{};
        ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        ci.bindingCount = 1;
        ci.pBindings    = &uboBinding;

        if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &descLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorSetLayout failed");
    }

    // ── Graphics pipelines ────────────────────────────────────────────────
    VkShaderModule makeShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule m;
        if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS)
            throw std::runtime_error("vkCreateShaderModule failed");
        return m;
    }

    void createGraphicsPipelines() {
        auto vertCode = readFile("shaders/vert.spv");
        auto fragCode = readFile("shaders/frag.spv");
        VkShaderModule vertMod = makeShaderModule(vertCode);
        VkShaderModule fragMod = makeShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName  = "main";

        VkVertexInputBindingDescription bind{};
        bind.binding   = 0;
        bind.stride    = 3 * sizeof(float);
        bind.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription attr{};
        attr.binding  = 0;
        attr.location = 0;
        attr.format   = VK_FORMAT_R32G32B32_SFLOAT;
        attr.offset   = 0;

        VkPipelineVertexInputStateCreateInfo vis{};
        vis.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vis.vertexBindingDescriptionCount   = 1;
        vis.pVertexBindingDescriptions      = &bind;
        vis.vertexAttributeDescriptionCount = 1;
        vis.pVertexAttributeDescriptions    = &attr;

        VkPipelineInputAssemblyStateCreateInfo ias{};
        ias.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0,0,(float)scExtent.width,(float)scExtent.height,0,1};
        VkRect2D   scissor{{0,0}, scExtent};
        VkPipelineViewportStateCreateInfo vs{};
        vs.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vs.viewportCount = 1; vs.pViewports = &viewport;
        vs.scissorCount  = 1; vs.pScissors  = &scissor;

        // shared rasterizer base
        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_BACK_BIT;
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable  = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp   = VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                             VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo cbs{};
        cbs.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cbs.attachmentCount = 1;
        cbs.pAttachments    = &cba;

        VkPipelineLayoutCreateInfo pli{};
        pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pli.setLayoutCount = 1;
        pli.pSetLayouts    = &descLayout;
        if (vkCreatePipelineLayout(device, &pli, nullptr, &pipeLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout failed");

        VkGraphicsPipelineCreateInfo pci{};
        pci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pci.stageCount          = 2;
        pci.pStages             = stages;
        pci.pVertexInputState   = &vis;
        pci.pInputAssemblyState = &ias;
        pci.pViewportState      = &vs;
        pci.pRasterizationState = &rs;
        pci.pMultisampleState   = &ms;
        pci.pDepthStencilState  = &ds;
        pci.pColorBlendState    = &cbs;
        pci.layout              = pipeLayout;
        pci.renderPass          = renderPass;
        pci.subpass             = 0;

        // Solid pipeline
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (solid) failed");

        // Wireframe pipeline
        rs.polygonMode = VK_POLYGON_MODE_LINE;
        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipelineWire) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines (wire) failed");

        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
    }

    // ── Depth resources ───────────────────────────────────────────────────
    uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
        for (uint32_t i=0; i<mp.memoryTypeCount; i++)
            if ((filter & (1<<i)) && (mp.memoryTypes[i].propertyFlags & props)==props)
                return i;
        throw std::runtime_error("No suitable memory type");
    }

    void createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                     VkImage& img, VkDeviceMemory& mem) {
        VkImageCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ci.imageType     = VK_IMAGE_TYPE_2D;
        ci.extent        = {w, h, 1};
        ci.mipLevels     = 1;
        ci.arrayLayers   = 1;
        ci.format        = fmt;
        ci.tiling        = VK_IMAGE_TILING_OPTIMAL;
        ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        ci.usage         = usage;
        ci.samples       = VK_SAMPLE_COUNT_1_BIT;
        ci.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateImage(device, &ci, nullptr, &img) != VK_SUCCESS)
            throw std::runtime_error("vkCreateImage failed");

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(device, img, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (image) failed");
        vkBindImageMemory(device, img, mem, 0);
    }

    void createDepthResources() {
        createImage(scExtent.width, scExtent.height,
                    VK_FORMAT_D32_SFLOAT,
                    VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                    depthImage, depthMemory);
        depthView = makeImageView(depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
    }

    // ── Framebuffers ──────────────────────────────────────────────────────
    void createFramebuffers() {
        framebuffers.resize(scViews.size());
        for (size_t i=0; i<scViews.size(); i++) {
            std::array<VkImageView,2> atts = {scViews[i], depthView};
            VkFramebufferCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = renderPass;
            ci.attachmentCount = (uint32_t)atts.size();
            ci.pAttachments    = atts.data();
            ci.width           = scExtent.width;
            ci.height          = scExtent.height;
            ci.layers          = 1;
            if (vkCreateFramebuffer(device, &ci, nullptr, &framebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    // ── Command pool ──────────────────────────────────────────────────────
    void createCommandPool() {
        auto qf = findQueueFamilies(physDev);
        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        ci.queueFamilyIndex = qf.graphics.value();
        if (vkCreateCommandPool(device, &ci, nullptr, &cmdPool) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed");
    }

    // ── Vertex buffer ─────────────────────────────────────────────────────
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem) {
        VkBufferCreateInfo ci{};
        ci.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        ci.size        = size;
        ci.usage       = usage;
        ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &ci, nullptr, &buf) != VK_SUCCESS)
            throw std::runtime_error("vkCreateBuffer failed");
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(device, buf, &req);
        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = req.size;
        ai.memoryTypeIndex = findMemType(req.memoryTypeBits, props);
        if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory (buffer) failed");
        vkBindBufferMemory(device, buf, mem, 0);
    }

    void copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandPool        = cmdPool;
        ai.commandBufferCount = 1;
        VkCommandBuffer cb;
        vkAllocateCommandBuffers(device, &ai, &cb);
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkBufferCopy region{0,0,size};
        vkCmdCopyBuffer(cb, src, dst, 1, &region);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{};
        si.sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);
        vkFreeCommandBuffers(device, cmdPool, 1, &cb);
    }

    void createVertexBuffer() {
        // Build the same face-culled mesh as the OpenGL version
        bool chunk[CHUNK_SIZE][CHUNK_SIZE][CHUNK_SIZE];
        for (int x=0;x<CHUNK_SIZE;x++)
            for (int y=0;y<CHUNK_SIZE;y++)
                for (int z=0;z<CHUNK_SIZE;z++)
                    chunk[x][y][z] = true;

        std::vector<float> verts;
        int neighbors[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        for (int x=0;x<CHUNK_SIZE;x++) for (int y=0;y<CHUNK_SIZE;y++) for (int z=0;z<CHUNK_SIZE;z++) {
            if (!chunk[x][y][z]) continue;
            for (int f=0;f<6;f++) {
                int nx=x+neighbors[f][0], ny=y+neighbors[f][1], nz=z+neighbors[f][2];
                if (nx<0||nx>=CHUNK_SIZE||ny<0||ny>=CHUNK_SIZE||nz<0||nz>=CHUNK_SIZE||!chunk[nx][ny][nz]) {
                    for (int v=0;v<18;v+=3) {
                        verts.push_back(faceVerts[f][v]   + x - CHUNK_SIZE/2.f);
                        verts.push_back(faceVerts[f][v+1] + y - CHUNK_SIZE/2.f);
                        verts.push_back(faceVerts[f][v+2] + z - CHUNK_SIZE/2.f);
                    }
                }
            }
        }
        vertexCount = (uint32_t)(verts.size() / 3);
        VkDeviceSize bufSize = verts.size() * sizeof(float);

        // Staging buffer
        VkBuffer stageBuf; VkDeviceMemory stageMem;
        createBuffer(bufSize,
                     VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     stageBuf, stageMem);
        void* data; vkMapMemory(device, stageMem, 0, bufSize, 0, &data);
        memcpy(data, verts.data(), bufSize);
        vkUnmapMemory(device, stageMem);

        createBuffer(bufSize,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     vertexBuffer, vertexMemory);
        copyBuffer(stageBuf, vertexBuffer, bufSize);
        vkDestroyBuffer(device, stageBuf, nullptr);
        vkFreeMemory(device, stageMem, nullptr);
    }

    // ── Uniform buffers ───────────────────────────────────────────────────
    void createUniformBuffers() {
        VkDeviceSize sz = sizeof(UBO);
        ubos.resize(MAX_FRAMES);
        uboMems.resize(MAX_FRAMES);
        uboMapped.resize(MAX_FRAMES);
        for (int i=0; i<MAX_FRAMES; i++) {
            createBuffer(sz,
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         ubos[i], uboMems[i]);
            vkMapMemory(device, uboMems[i], 0, sz, 0, &uboMapped[i]);
        }
    }

    void updateUniformBuffer(uint32_t frame) {
        vec3 front = {
            cosf(glm_rad(camera.yaw)) * cosf(glm_rad(camera.pitch)),
            sinf(glm_rad(camera.pitch)),
            sinf(glm_rad(camera.yaw)) * cosf(glm_rad(camera.pitch))
        };
        glm_vec3_normalize(front);
        vec3 center; glm_vec3_add(camera.position, front, center);
        vec3 up = {0,1,0};

        mat4 view; glm_lookat(camera.position, center, up, view);
        mat4 proj; glm_perspective(glm_rad(45.f),
                                   (float)scExtent.width / (float)scExtent.height,
                                   0.1f, 500.f, proj);
        // cglm uses OpenGL convention; flip Y for Vulkan
        proj[1][1] *= -1;

        UBO ubo{}; glm_mat4_mul(proj, view, ubo.mvp);
        memcpy(uboMapped[frame], &ubo, sizeof(ubo));
    }

    // ── Descriptor pool & sets ────────────────────────────────────────────
    void createDescriptorPool() {
        VkDescriptorPoolSize ps{};
        ps.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        ps.descriptorCount = (uint32_t)MAX_FRAMES;
        VkDescriptorPoolCreateInfo ci{};
        ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        ci.poolSizeCount = 1;
        ci.pPoolSizes    = &ps;
        ci.maxSets       = (uint32_t)MAX_FRAMES;
        if (vkCreateDescriptorPool(device, &ci, nullptr, &descPool) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDescriptorPool failed");
    }

    void createDescriptorSets() {
        std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES, descLayout);
        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = descPool;
        ai.descriptorSetCount = (uint32_t)MAX_FRAMES;
        ai.pSetLayouts        = layouts.data();
        descSets.resize(MAX_FRAMES);
        if (vkAllocateDescriptorSets(device, &ai, descSets.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateDescriptorSets failed");

        for (int i=0; i<MAX_FRAMES; i++) {
            VkDescriptorBufferInfo bi{};
            bi.buffer = ubos[i];
            bi.offset = 0;
            bi.range  = sizeof(UBO);
            VkWriteDescriptorSet w{};
            w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            w.dstSet          = descSets[i];
            w.dstBinding      = 0;
            w.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            w.descriptorCount = 1;
            w.pBufferInfo     = &bi;
            vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);
        }
    }

    // ── Command buffers ───────────────────────────────────────────────────
    void createCommandBuffers() {
        cmdBufs.resize(MAX_FRAMES);
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = cmdPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = (uint32_t)MAX_FRAMES;
        if (vkAllocateCommandBuffers(device, &ai, cmdBufs.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx) {
        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
            throw std::runtime_error("vkBeginCommandBuffer failed");

        std::array<VkClearValue,2> clears{};
        clears[0].color        = {{0.1f,0.2f,0.3f,1.f}};
        clears[1].depthStencil = {1.f, 0};

        VkRenderPassBeginInfo rbi{};
        rbi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rbi.renderPass        = renderPass;
        rbi.framebuffer       = framebuffers[imgIdx];
        rbi.renderArea.offset = {0,0};
        rbi.renderArea.extent = scExtent;
        rbi.clearValueCount   = (uint32_t)clears.size();
        rbi.pClearValues      = clears.data();

        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          wireframe ? pipelineWire : pipeline);
        VkBuffer vbufs[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vbufs, offsets);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipeLayout, 0, 1, &descSets[currentFrame], 0, nullptr);
        vkCmdDraw(cb, vertexCount, 1, 0, 0);
        vkCmdEndRenderPass(cb);

        if (vkEndCommandBuffer(cb) != VK_SUCCESS)
            throw std::runtime_error("vkEndCommandBuffer failed");
    }

    // ── Sync objects ──────────────────────────────────────────────────────
    void createSyncObjects() {
        // imgAvail must be per-swapchain-image, not per-frame-in-flight,
        // to avoid reusing a semaphore still in use by the presentation engine.
        imgAvail.resize(scImages.size());
        renderDone.resize(MAX_FRAMES);
        inFlight.resize(MAX_FRAMES);
        VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (size_t i=0; i<scImages.size(); i++)
            if (vkCreateSemaphore(device,&si,nullptr,&imgAvail[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateSemaphore (imgAvail) failed");
        for (int i=0; i<MAX_FRAMES; i++) {
            if (vkCreateSemaphore(device,&si,nullptr,&renderDone[i]) != VK_SUCCESS ||
                vkCreateFence(device,&fi,nullptr,&inFlight[i])       != VK_SUCCESS)
                throw std::runtime_error("vkCreate sync objects failed");
        }
    }

    // ─────────────────────────────────────────────────────────────────────
    // Swapchain recreation (resize)
    // ─────────────────────────────────────────────────────────────────────
    void cleanupSwapchain() {
        vkDestroyImageView(device, depthView, nullptr);
        vkDestroyImage(device, depthImage, nullptr);
        vkFreeMemory(device, depthMemory, nullptr);
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv : scViews)      vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
    }

    void recreateSwapchain() {
        int w=0,h=0;
        while (w==0||h==0) { glfwGetFramebufferSize(window,&w,&h); glfwWaitEvents(); }
        vkDeviceWaitIdle(device);
        cleanupSwapchain();
        createSwapchain();
        createImageViews();
        createDepthResources();
        createFramebuffers();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Main loop
    // ─────────────────────────────────────────────────────────────────────
    void mainLoop() {
        while (!glfwWindowShouldClose(window)) {
            float now = (float)glfwGetTime();
            deltaTime = now - lastFrame;
            lastFrame = now;

            glfwPollEvents();
            processInput();
            drawFrame();
        }
        vkDeviceWaitIdle(device);
    }

    void drawFrame() {
        vkWaitForFences(device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);

        // Acquire using a per-swapchain-image semaphore to avoid reuse while in flight
        uint32_t imgIdx;
        VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                              imgAvail[imgAvailIdx], VK_NULL_HANDLE, &imgIdx);
        if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return; }
        else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
            throw std::runtime_error("vkAcquireNextImageKHR failed");

        vkResetFences(device, 1, &inFlight[currentFrame]);
        vkResetCommandBuffer(cmdBufs[currentFrame], 0);

        updateUniformBuffer(currentFrame);
        recordCommandBuffer(cmdBufs[currentFrame], imgIdx);

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &imgAvail[imgAvailIdx];
        si.pWaitDstStageMask    = &waitStage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &cmdBufs[currentFrame];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &renderDone[currentFrame];
        if (vkQueueSubmit(graphicsQueue, 1, &si, inFlight[currentFrame]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed");

        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &renderDone[currentFrame];
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &swapchain;
        pi.pImageIndices      = &imgIdx;
        res = vkQueuePresentKHR(presentQueue, &pi);
        if (res==VK_ERROR_OUT_OF_DATE_KHR || res==VK_SUBOPTIMAL_KHR || framebufferResized) {
            framebufferResized = false;
            recreateSwapchain();
        }
        currentFrame = (currentFrame + 1) % MAX_FRAMES;
        imgAvailIdx  = (imgAvailIdx  + 1) % (uint32_t)scImages.size();
    }

    // ─────────────────────────────────────────────────────────────────────
    // Cleanup
    // ─────────────────────────────────────────────────────────────────────
    void cleanup() {
        cleanupSwapchain();
        for (int i=0; i<MAX_FRAMES; i++) {
            vkDestroyBuffer(device, ubos[i], nullptr);
            vkFreeMemory(device, uboMems[i], nullptr);
        }
        vkDestroyDescriptorPool(device, descPool, nullptr);
        vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory(device, vertexMemory, nullptr);
        vkDestroyPipeline(device, pipeline, nullptr);
        vkDestroyPipeline(device, pipelineWire, nullptr);
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
        vkDestroyRenderPass(device, renderPass, nullptr);
        for (int i=0; i<MAX_FRAMES; i++) {
            vkDestroySemaphore(device, renderDone[i], nullptr);
            vkDestroyFence(device, inFlight[i], nullptr);
        }
        for (size_t i=0; i<imgAvail.size(); i++)
            vkDestroySemaphore(device, imgAvail[i], nullptr);
        vkDestroyCommandPool(device, cmdPool, nullptr);
        vkDestroyDevice(device, nullptr);
        if (ENABLE_VALID) {
            auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)
                vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT");
            if (fn) fn(instance, debugMessenger, nullptr);
        }
        vkDestroySurfaceKHR(instance, surface, nullptr);
        vkDestroyInstance(instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    try {
        App app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}