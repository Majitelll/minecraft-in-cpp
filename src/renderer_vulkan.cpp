#include "renderer_vulkan.h"
#include "textureatlas.h"
#include "log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────────────────
// Config
// ─────────────────────────────────────────────────────────────────────────────
#ifdef NDEBUG
static constexpr bool ENABLE_VALID = false;
#else
static constexpr bool ENABLE_VALID = true;
#endif

static const std::vector<const char*> VALIDATION_LAYERS = {
    "VK_LAYER_KHRONOS_validation"
};
static const std::vector<const char*> DEVICE_EXTENSIONS = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME
};

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT,
    const VkDebugUtilsMessengerCallbackDataEXT* d, void*)
{
    std::cerr << "[Vulkan] " << d->pMessage << "\n";
    return VK_FALSE;
}

static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open()) throw std::runtime_error("Cannot open: " + path);
    size_t sz = (size_t)f.tellg();
    std::vector<char> buf(sz);
    f.seekg(0); f.read(buf.data(), sz);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::init(GLFWwindow* win) {
    window = win;
    logMsg("initVulkan...");
    initVulkan();
}

void VulkanRenderer::waitIdle() {
    vkDeviceWaitIdle(device);
}

void VulkanRenderer::onWindowResized() {
    recreateSwapchain();
}

// ─────────────────────────────────────────────────────────────────────────────
// initVulkan
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::initVulkan() {
    logMsg("  createInstance");       createInstance();
    logMsg("  setupDebugMessenger");  setupDebugMessenger();
    logMsg("  createSurface");        createSurface();
    logMsg("  pickPhysicalDevice");   pickPhysicalDevice();
    logMsg("  createLogicalDevice");  createLogicalDevice();
    logMsg("  createSwapchain");      createSwapchain();
    logMsg("  createImageViews");     createImageViews();
    logMsg("  createRenderPass");     createRenderPass();
    logMsg("  createDescLayout");     createDescriptorSetLayout();
    logMsg("  createPipelines");      createGraphicsPipelines();
    logMsg("  createDepth");          createDepthResources();
    logMsg("  createFramebuffers");   createFramebuffers();
    logMsg("  createCommandPool");    createCommandPool();
    logMsg("  createTextureAtlas");   createTextureAtlas();
    logMsg("  createUniformBuffers"); createUniformBuffers();
    logMsg("  createDescPool");       createDescriptorPool();
    logMsg("  createDescSets");       createDescriptorSets();
    logMsg("  createCommandBuffers"); createCommandBuffers();
    logMsg("  createSyncObjects");    createSyncObjects();
    logMsg("  createUIPipeline");     createUIPipeline();
    logMsg("  initVulkan done");
}

// ─────────────────────────────────────────────────────────────────────────────
// drawFrame
// ─────────────────────────────────────────────────────────────────────────────
bool VulkanRenderer::drawFrame(const Camera& camera, bool wireframe) {
    bool log0 = firstFrame;
    firstFrame = false;

    if (log0) logMsg("  drawFrame: waitForFences");
    vkWaitForFences(device, 1, &inFlight[currentFrame], VK_TRUE, UINT64_MAX);

    if (log0) logMsg("  drawFrame: acquireNextImage");
    uint32_t imgIdx;
    VkResult res = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                                          imgAvail[imgAvailIdx],
                                          VK_NULL_HANDLE, &imgIdx);
    if (res == VK_ERROR_OUT_OF_DATE_KHR) { recreateSwapchain(); return true; }
    else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
        throw std::runtime_error("vkAcquireNextImageKHR failed");

    if (log0) logMsg("  drawFrame: resetFences + recordCommandBuffer");
    vkResetFences(device, 1, &inFlight[currentFrame]);
    vkResetCommandBuffer(cmdBufs[currentFrame], 0);
    updateUniformBuffer(currentFrame, camera);
    recordCommandBuffer(cmdBufs[currentFrame], imgIdx, wireframe);

    if (log0) logMsg("  drawFrame: queueSubmit");
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

    if (log0) logMsg("  drawFrame: queuePresent");
    VkPresentInfoKHR pi{};
    pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores    = &renderDone[currentFrame];
    pi.swapchainCount     = 1;
    pi.pSwapchains        = &swapchain;
    pi.pImageIndices      = &imgIdx;
    res = vkQueuePresentKHR(presentQueue, &pi);

    bool swapchainRecreated = false;
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR) {
        recreateSwapchain();
        swapchainRecreated = true;
    } else if (res != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    if (log0) logMsg("  drawFrame: done");
    currentFrame = (currentFrame + 1) % MAX_FRAMES;
    imgAvailIdx  = (imgAvailIdx  + 1) % (uint32_t)scImages.size();

    return swapchainRecreated;
}

// ─────────────────────────────────────────────────────────────────────────────
// Chunk GPU management
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::uploadChunkMesh(const UploadRequest& req) {
    logMsg("uploadChunkMesh: (" + std::to_string(req.pos.x) + "," + std::to_string(req.pos.z) + ") verts=" + std::to_string(req.vertices.size()));
    destroyChunkBuffers(req.pos);

    VkDeviceSize bufSize = req.vertices.size() * sizeof(float);
    if (bufSize == 0) return;

    VkBuffer stageBuf; VkDeviceMemory stageMem;
    createBuffer(bufSize,
                 VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void* data; vkMapMemory(device, stageMem, 0, bufSize, 0, &data);
    memcpy(data, req.vertices.data(), (size_t)bufSize);
    vkUnmapMemory(device, stageMem);

    ChunkBuffers cb{};
    createBuffer(bufSize,
                 VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 cb.vertexBuffer, cb.vertexMemory);
    copyBuffer(stageBuf, cb.vertexBuffer, bufSize);
    cb.vertexCount = (uint32_t)(req.vertices.size() / 6);

    vkDestroyBuffer(device, stageBuf, nullptr);
    vkFreeMemory(device, stageMem, nullptr);

    chunkBuffers[req.pos] = cb;
}

void VulkanRenderer::destroyChunkBuffers(ChunkPos pos) {
    auto it = chunkBuffers.find(pos);
    if (it == chunkBuffers.end()) return;
    logMsg("destroyChunkBuffers: (" + std::to_string(pos.x) + "," + std::to_string(pos.z) + ")");
    vkDeviceWaitIdle(device);
    vkDestroyBuffer(device, it->second.vertexBuffer, nullptr);
    vkFreeMemory(device, it->second.vertexMemory, nullptr);
    chunkBuffers.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI vertices
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::uploadUIVertices(const std::vector<float>& verts) {
    if (uiVertexBuf != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        vkDestroyBuffer(device, uiVertexBuf, nullptr);
        vkFreeMemory(device, uiVertexMem, nullptr);
        uiVertexBuf = VK_NULL_HANDLE;
    }

    if (verts.empty()) return;

    uiVertexCount = (uint32_t)(verts.size() / 4);

    VkDeviceSize bufSize = verts.size() * sizeof(float);
    createBuffer(bufSize,
                 VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 uiVertexBuf, uiVertexMem);
    void* data; vkMapMemory(device, uiVertexMem, 0, bufSize, 0, &data);
    memcpy(data, verts.data(), (size_t)bufSize);
    vkUnmapMemory(device, uiVertexMem);
}

// ─────────────────────────────────────────────────────────────────────────────
// Instance — macOS / MoltenVK portability
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createInstance() {
    if (ENABLE_VALID && !checkValidationSupport())
        throw std::runtime_error("Validation layers not available");

    VkApplicationInfo ai{};
    ai.sType            = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    ai.pApplicationName = "Minecraft";
    ai.apiVersion       = VK_API_VERSION_1_0;

    auto exts = getRequiredExtensions();

    VkInstanceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    ci.pApplicationInfo        = &ai;
    ci.enabledExtensionCount   = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();

    // Required on macOS with MoltenVK to enumerate portability drivers
#ifdef __APPLE__
    ci.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
#endif

    if (ENABLE_VALID) {
        ci.enabledLayerCount   = (uint32_t)VALIDATION_LAYERS.size();
        ci.ppEnabledLayerNames = VALIDATION_LAYERS.data();
    }
    if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
        throw std::runtime_error("vkCreateInstance failed");
}

bool VulkanRenderer::checkValidationSupport() {
    uint32_t n; vkEnumerateInstanceLayerProperties(&n, nullptr);
    std::vector<VkLayerProperties> layers(n);
    vkEnumerateInstanceLayerProperties(&n, layers.data());
    for (auto* name : VALIDATION_LAYERS) {
        bool found = false;
        for (auto& l : layers) if (strcmp(name, l.layerName)==0) { found=true; break; }
        if (!found) return false;
    }
    return true;
}

std::vector<const char*> VulkanRenderer::getRequiredExtensions() {
    uint32_t n; const char** glfwExts = glfwGetRequiredInstanceExtensions(&n);
    std::vector<const char*> exts(glfwExts, glfwExts+n);
    if (ENABLE_VALID) exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    // MoltenVK portability extensions (macOS)
#ifdef __APPLE__
    exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    exts.push_back("VK_KHR_get_physical_device_properties2");
#endif
    return exts;
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug messenger
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::setupDebugMessenger() {
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

// ─────────────────────────────────────────────────────────────────────────────
// Surface
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createSurface() {
    if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
        throw std::runtime_error("glfwCreateWindowSurface failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Physical device
// ─────────────────────────────────────────────────────────────────────────────
VulkanRenderer::QueueFamilies VulkanRenderer::findQueueFamilies(VkPhysicalDevice dev) {
    QueueFamilies qf;
    uint32_t n; vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, nullptr);
    std::vector<VkQueueFamilyProperties> props(n);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &n, props.data());
    for (uint32_t i=0; i<n; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) qf.graphics = i;
        VkBool32 ps=false; vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, surface, &ps);
        if (ps) qf.present = i;
        if (qf.complete()) break;
    }
    return qf;
}

bool VulkanRenderer::deviceSuitable(VkPhysicalDevice dev) {
    if (!findQueueFamilies(dev).complete()) return false;
    uint32_t n; vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateDeviceExtensionProperties(dev, nullptr, &n, exts.data());

    // Build required device extensions list (add portability subset on macOS if present)
    std::vector<const char*> required = DEVICE_EXTENSIONS;
#ifdef __APPLE__
    for (auto& e : exts)
        if (strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
            required.push_back("VK_KHR_portability_subset");
            break;
        }
#endif
    for (auto* req : required) {
        bool found = false;
        for (auto& e : exts) if (strcmp(req, e.extensionName)==0) { found=true; break; }
        if (!found) return false;
    }
    uint32_t fmtN, pmN;
    vkGetPhysicalDeviceSurfaceFormatsKHR(dev, surface, &fmtN, nullptr);
    vkGetPhysicalDeviceSurfacePresentModesKHR(dev, surface, &pmN, nullptr);
    if (fmtN == 0 || pmN == 0) return false;

    // The wireframe pipeline requires fillModeNonSolid; reject devices that
    // don't support it rather than letting vkCreateDevice fail later.
    VkPhysicalDeviceFeatures feats{};
    vkGetPhysicalDeviceFeatures(dev, &feats);
    return feats.fillModeNonSolid == VK_TRUE;
}

void VulkanRenderer::pickPhysicalDevice() {
    uint32_t n; vkEnumeratePhysicalDevices(instance, &n, nullptr);
    if (n == 0) throw std::runtime_error("No Vulkan GPUs found");
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(instance, &n, devs.data());
    for (auto& d : devs) if (deviceSuitable(d)) { physDev = d; return; }
    throw std::runtime_error("No suitable GPU found");
}

// ─────────────────────────────────────────────────────────────────────────────
// Logical device
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createLogicalDevice() {
    auto qf = findQueueFamilies(physDev);
    std::set<uint32_t> uniqueQ = {qf.graphics.value(), qf.present.value()};
    float prio = 1.f;
    std::vector<VkDeviceQueueCreateInfo> qcis;
    for (uint32_t q : uniqueQ) {
        VkDeviceQueueCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        ci.queueFamilyIndex = q; ci.queueCount = 1; ci.pQueuePriorities = &prio;
        qcis.push_back(ci);
    }
    VkPhysicalDeviceFeatures feat{}; feat.fillModeNonSolid = VK_TRUE;

    // Collect device extensions — add portability subset on macOS if available
    std::vector<const char*> devExts = DEVICE_EXTENSIONS;
#ifdef __APPLE__
    uint32_t en; vkEnumerateDeviceExtensionProperties(physDev, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> avail(en);
    vkEnumerateDeviceExtensionProperties(physDev, nullptr, &en, avail.data());
    for (auto& e : avail)
        if (strcmp(e.extensionName, "VK_KHR_portability_subset") == 0) {
            devExts.push_back("VK_KHR_portability_subset"); break;
        }
#endif

    VkDeviceCreateInfo ci{};
    ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    ci.queueCreateInfoCount    = (uint32_t)qcis.size();
    ci.pQueueCreateInfos       = qcis.data();
    ci.enabledExtensionCount   = (uint32_t)devExts.size();
    ci.ppEnabledExtensionNames = devExts.data();
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

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain
// ─────────────────────────────────────────────────────────────────────────────
VkSurfaceFormatKHR VulkanRenderer::chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& fmts) {
    for (auto& f : fmts)
        if (f.format==VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) return f;
    return fmts[0];
}
VkPresentModeKHR VulkanRenderer::choosePresentMode(const std::vector<VkPresentModeKHR>& modes) {
    for (auto& m : modes) if (m==VK_PRESENT_MODE_MAILBOX_KHR) return m;
    return VK_PRESENT_MODE_FIFO_KHR;
}
VkExtent2D VulkanRenderer::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) {
    if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) return caps.currentExtent;
    int w,h; glfwGetFramebufferSize(window, &w, &h);
    VkExtent2D e={(uint32_t)w,(uint32_t)h};
    e.width  = std::clamp(e.width,  caps.minImageExtent.width,  caps.maxImageExtent.width);
    e.height = std::clamp(e.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    return e;
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);
    uint32_t n;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> fmts(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &n, fmts.data());
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &n, nullptr);
    std::vector<VkPresentModeKHR> modes(n);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDev, surface, &n, modes.data());

    auto fmt = chooseSurfaceFormat(fmts);
    auto pm  = choosePresentMode(modes);
    auto ext = chooseExtent(caps);
    uint32_t imgCount = std::min(caps.minImageCount + 1,
                                 caps.maxImageCount > 0 ? caps.maxImageCount : UINT32_MAX);

    VkSwapchainCreateInfoKHR ci{};
    ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    ci.surface = surface; ci.minImageCount = imgCount;
    ci.imageFormat = fmt.format; ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = ext; ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto qf = findQueueFamilies(physDev);
    uint32_t qfIdx[] = {qf.graphics.value(), qf.present.value()};
    if (qf.graphics.value() != qf.present.value()) {
        ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        ci.queueFamilyIndexCount = 2; ci.pQueueFamilyIndices = qfIdx;
    } else { ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; }
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = pm; ci.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &ci, nullptr, &swapchain) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSwapchainKHR failed");

    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, nullptr);
    scImages.resize(imgCount);
    vkGetSwapchainImagesKHR(device, swapchain, &imgCount, scImages.data());
    scFormat = fmt.format; scExtent = ext;
}

// ─────────────────────────────────────────────────────────────────────────────
// Image views
// ─────────────────────────────────────────────────────────────────────────────
VkImageView VulkanRenderer::makeImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    ci.image = img; ci.viewType = VK_IMAGE_VIEW_TYPE_2D; ci.format = fmt;
    ci.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView v;
    if (vkCreateImageView(device, &ci, nullptr, &v) != VK_SUCCESS)
        throw std::runtime_error("vkCreateImageView failed");
    return v;
}
void VulkanRenderer::createImageViews() {
    scViews.resize(scImages.size());
    for (size_t i=0; i<scImages.size(); i++)
        scViews[i] = makeImageView(scImages[i], scFormat, VK_IMAGE_ASPECT_COLOR_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Render pass
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = scFormat; color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = VK_FORMAT_D32_SFLOAT; depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1; sub.pColorAttachments = &colorRef;
    sub.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    std::array<VkAttachmentDescription,2> atts = {color, depth};
    VkRenderPassCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    ci.attachmentCount = (uint32_t)atts.size(); ci.pAttachments = atts.data();
    ci.subpassCount = 1; ci.pSubpasses = &sub;
    ci.dependencyCount = 1; ci.pDependencies = &dep;
    if (vkCreateRenderPass(device, &ci, nullptr, &renderPass) != VK_SUCCESS)
        throw std::runtime_error("vkCreateRenderPass failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor layout
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding         = 0;
    uboBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding         = 1;
    samplerBinding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding,2> bindings = {uboBinding, samplerBinding};
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.bindingCount = (uint32_t)bindings.size();
    ci.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &ci, nullptr, &descLayout) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorSetLayout failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipelines
// ─────────────────────────────────────────────────────────────────────────────
VkShaderModule VulkanRenderer::makeShaderModule(const std::vector<char>& code) {
    VkShaderModuleCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = code.size(); ci.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule m;
    if (vkCreateShaderModule(device, &ci, nullptr, &m) != VK_SUCCESS)
        throw std::runtime_error("vkCreateShaderModule failed");
    return m;
}

void VulkanRenderer::createGraphicsPipelines() {
    auto vertCode = readFile("shaders/vert.spv");
    auto fragCode = readFile("shaders/frag.spv");
    VkShaderModule vertMod = makeShaderModule(vertCode);
    VkShaderModule fragMod = makeShaderModule(fragCode);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertMod, "main", nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragMod, "main", nullptr};

    // Vertex format: xyz uvRaw tileBaseU (6 floats per vertex)
    VkVertexInputBindingDescription bind{0, 6*sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[3] = {
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0},               // pos
        {1, 0, VK_FORMAT_R32G32_SFLOAT,    3*sizeof(float)},  // uvRaw
        {2, 0, VK_FORMAT_R32_SFLOAT,       5*sizeof(float)},  // tileBaseU
    };
    VkPipelineVertexInputStateCreateInfo vis{};
    vis.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vis.vertexBindingDescriptionCount = 1; vis.pVertexBindingDescriptions = &bind;
    vis.vertexAttributeDescriptionCount = 3; vis.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ias{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    std::vector<VkDynamicState> dynStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = (uint32_t)dynStates.size(); dyn.pDynamicStates = dynStates.data();

    VkPipelineViewportStateCreateInfo vs{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs.viewportCount = 1; vs.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_BACK_BIT;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable = VK_TRUE; ds.depthWriteEnable = VK_TRUE; ds.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                         VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1; cbs.pAttachments = &cba;

    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1; pli.pSetLayouts = &descLayout;
    if (vkCreatePipelineLayout(device, &pli, nullptr, &pipeLayout) != VK_SUCCESS)
        throw std::runtime_error("vkCreatePipelineLayout failed");

    VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pci.stageCount = 2; pci.pStages = stages;
    pci.pVertexInputState = &vis; pci.pInputAssemblyState = &ias;
    pci.pViewportState = &vs; pci.pRasterizationState = &rs;
    pci.pMultisampleState = &ms; pci.pDepthStencilState = &ds;
    pci.pColorBlendState = &cbs; pci.pDynamicState = &dyn;
    pci.layout = pipeLayout; pci.renderPass = renderPass; pci.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines (solid) failed");

    rs.polygonMode = VK_POLYGON_MODE_LINE;
    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pci, nullptr, &pipelineWire) != VK_SUCCESS)
        throw std::runtime_error("vkCreateGraphicsPipelines (wire) failed");

    vkDestroyShaderModule(device, vertMod, nullptr);
    vkDestroyShaderModule(device, fragMod, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Depth
// ─────────────────────────────────────────────────────────────────────────────
uint32_t VulkanRenderer::findMemType(uint32_t filter, VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
    for (uint32_t i=0; i<mp.memoryTypeCount; i++)
        if ((filter&(1<<i)) && (mp.memoryTypes[i].propertyFlags&props)==props) return i;
    throw std::runtime_error("No suitable memory type");
}
void VulkanRenderer::createImage(uint32_t w, uint32_t h, VkFormat fmt, VkImageUsageFlags usage,
                                 VkImage& img, VkDeviceMemory& mem) {
    VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ci.imageType = VK_IMAGE_TYPE_2D; ci.extent = {w,h,1};
    ci.mipLevels = 1; ci.arrayLayers = 1; ci.format = fmt;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage;
    ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device, &ci, nullptr, &img) != VK_SUCCESS)
        throw std::runtime_error("vkCreateImage failed");
    VkMemoryRequirements req; vkGetImageMemoryRequirements(device, img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory (image) failed");
    vkBindImageMemory(device, img, mem, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Atlas
// ─────────────────────────────────────────────────────────────────────────────
static void transitionImageLayout(VkDevice device, VkCommandPool pool,
                                   VkQueue queue, VkImage image,
                                   VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandPool = pool; ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(device, &ai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout; barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkPipelineStageFlags srcStage, dstStage;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(cb, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cb);
}

void VulkanRenderer::createTextureAtlas() {
    auto pixels = generateAtlas(); // RGBA8 pixel data
    VkDeviceSize imgSize = ATLAS_W * ATLAS_H * 4;

    // Staging buffer
    VkBuffer stageBuf; VkDeviceMemory stageMem;
    createBuffer(imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stageBuf, stageMem);
    void* data; vkMapMemory(device, stageMem, 0, imgSize, 0, &data);
    memcpy(data, pixels.data(), (size_t)imgSize);
    vkUnmapMemory(device, stageMem);

    // Create image
    createImage(ATLAS_W, ATLAS_H, VK_FORMAT_R8G8B8A8_SRGB,
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                atlasImage, atlasMemory);

    // Transition + copy
    transitionImageLayout(device, cmdPool, graphicsQueue, atlasImage,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandPool = cmdPool; ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(device, &ai, &cb);
    VkCommandBufferBeginInfo begi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begi);
    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent      = {(uint32_t)ATLAS_W, (uint32_t)ATLAS_H, 1};
    vkCmdCopyBufferToImage(cb, stageBuf, atlasImage,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cb);

    transitionImageLayout(device, cmdPool, graphicsQueue, atlasImage,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    vkDestroyBuffer(device, stageBuf, nullptr);
    vkFreeMemory(device, stageMem, nullptr);

    // Image view
    atlasView = makeImageView(atlasImage, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_ASPECT_COLOR_BIT);

    // Sampler — nearest filter for pixel art look
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter  = VK_FILTER_NEAREST;
    sci.minFilter  = VK_FILTER_NEAREST;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 0.f;
    if (vkCreateSampler(device, &sci, nullptr, &atlasSampler) != VK_SUCCESS)
        throw std::runtime_error("vkCreateSampler failed");
}

void VulkanRenderer::createDepthResources() {
    createImage(scExtent.width, scExtent.height, VK_FORMAT_D32_SFLOAT,
                VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, depthImage, depthMemory);
    depthView = makeImageView(depthImage, VK_FORMAT_D32_SFLOAT, VK_IMAGE_ASPECT_DEPTH_BIT);
}

// ─────────────────────────────────────────────────────────────────────────────
// Framebuffers
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createFramebuffers() {
    framebuffers.resize(scViews.size());
    for (size_t i=0; i<scViews.size(); i++) {
        std::array<VkImageView,2> atts = {scViews[i], depthView};
        VkFramebufferCreateInfo ci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        ci.renderPass = renderPass; ci.attachmentCount = (uint32_t)atts.size();
        ci.pAttachments = atts.data(); ci.width = scExtent.width;
        ci.height = scExtent.height; ci.layers = 1;
        if (vkCreateFramebuffer(device, &ci, nullptr, &framebuffers[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateFramebuffer failed");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Command pool
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createCommandPool() {
    auto qf = findQueueFamilies(physDev);
    VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    ci.queueFamilyIndex = qf.graphics.value();
    if (vkCreateCommandPool(device, &ci, nullptr, &cmdPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateCommandPool failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Buffer helpers
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props, VkBuffer& buf, VkDeviceMemory& mem) {
    VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &buf) != VK_SUCCESS)
        throw std::runtime_error("vkCreateBuffer failed");
    VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size; ai.memoryTypeIndex = findMemType(req.memoryTypeBits, props);
    if (vkAllocateMemory(device, &ai, nullptr, &mem) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateMemory failed");
    vkBindBufferMemory(device, buf, mem, 0);
}
void VulkanRenderer::copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandPool = cmdPool; ai.commandBufferCount = 1;
    VkCommandBuffer cb; vkAllocateCommandBuffers(device, &ai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkBufferCopy region{0,0,size}; vkCmdCopyBuffer(cb, src, dst, 1, &region);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1; si.pCommandBuffers = &cb;
    vkQueueSubmit(graphicsQueue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);
    vkFreeCommandBuffers(device, cmdPool, 1, &cb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Uniform buffers
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createUniformBuffers() {
    ubos.resize(MAX_FRAMES); uboMems.resize(MAX_FRAMES); uboMapped.resize(MAX_FRAMES);
    for (int i=0; i<MAX_FRAMES; i++) {
        createBuffer(sizeof(UBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     ubos[i], uboMems[i]);
        vkMapMemory(device, uboMems[i], 0, sizeof(UBO), 0, &uboMapped[i]);
    }
}
void VulkanRenderer::updateUniformBuffer(uint32_t frame, const Camera& camera) {
    vec3 front = {
        cosf(glm_rad(camera.yaw))*cosf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.pitch)),
        sinf(glm_rad(camera.yaw))*cosf(glm_rad(camera.pitch))
    };
    glm_vec3_normalize(front);
    vec3 center; glm_vec3_add((float*)camera.position, front, center);
    vec3 up = {0.f,1.f,0.f};
    mat4 view; glm_lookat((float*)camera.position, center, up, view);
    mat4 proj; glm_perspective(glm_rad(70.f),
                               (float)scExtent.width/(float)scExtent.height,
                               0.1f, 2000.f, proj);
    proj[1][1] *= -1;
    UBO ubo{}; glm_mat4_mul(proj, view, ubo.mvp);
    memcpy(uboMapped[frame], &ubo, sizeof(ubo));
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor pool & sets
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createDescriptorPool() {
    std::array<VkDescriptorPoolSize,2> sizes{};
    sizes[0] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          (uint32_t)MAX_FRAMES};
    sizes[1] = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  (uint32_t)MAX_FRAMES};
    VkDescriptorPoolCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    ci.poolSizeCount = (uint32_t)sizes.size();
    ci.pPoolSizes    = sizes.data();
    ci.maxSets       = (uint32_t)MAX_FRAMES;
    if (vkCreateDescriptorPool(device, &ci, nullptr, &descPool) != VK_SUCCESS)
        throw std::runtime_error("vkCreateDescriptorPool failed");
}
void VulkanRenderer::createDescriptorSets() {
    std::vector<VkDescriptorSetLayout> layouts(MAX_FRAMES, descLayout);
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = descPool; ai.descriptorSetCount = (uint32_t)MAX_FRAMES;
    ai.pSetLayouts = layouts.data();
    descSets.resize(MAX_FRAMES);
    if (vkAllocateDescriptorSets(device, &ai, descSets.data()) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateDescriptorSets failed");
    for (int i=0; i<MAX_FRAMES; i++) {
        VkDescriptorBufferInfo bi{ubos[i], 0, sizeof(UBO)};
        VkWriteDescriptorSet wUBO{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wUBO.dstSet = descSets[i]; wUBO.dstBinding = 0;
        wUBO.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        wUBO.descriptorCount = 1; wUBO.pBufferInfo = &bi;

        VkDescriptorImageInfo ii{};
        ii.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        ii.imageView   = atlasView;
        ii.sampler     = atlasSampler;
        VkWriteDescriptorSet wSampler{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        wSampler.dstSet = descSets[i]; wSampler.dstBinding = 1;
        wSampler.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wSampler.descriptorCount = 1; wSampler.pImageInfo = &ii;

        std::array<VkWriteDescriptorSet,2> writes = {wUBO, wSampler};
        vkUpdateDescriptorSets(device, (uint32_t)writes.size(), writes.data(), 0, nullptr);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Command buffers
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createCommandBuffers() {
    cmdBufs.resize(MAX_FRAMES);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = cmdPool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = (uint32_t)MAX_FRAMES;
    if (vkAllocateCommandBuffers(device, &ai, cmdBufs.data()) != VK_SUCCESS)
        throw std::runtime_error("vkAllocateCommandBuffers failed");
}

void VulkanRenderer::recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx, bool wireframe) {
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    if (vkBeginCommandBuffer(cb, &bi) != VK_SUCCESS)
        throw std::runtime_error("vkBeginCommandBuffer failed");

    std::array<VkClearValue,2> clears{};
    clears[0].color        = {{0.53f, 0.81f, 0.98f, 1.f}};
    clears[1].depthStencil = {1.f, 0};

    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = renderPass; rbi.framebuffer = framebuffers[imgIdx];
    rbi.renderArea = {{0,0}, scExtent};
    rbi.clearValueCount = (uint32_t)clears.size(); rbi.pClearValues = clears.data();

    vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      wireframe ? pipelineWire : pipeline);

    VkViewport viewport{0.f, 0.f, (float)scExtent.width, (float)scExtent.height, 0.f, 1.f};
    vkCmdSetViewport(cb, 0, 1, &viewport);
    VkRect2D scissor{{0,0}, scExtent};
    vkCmdSetScissor(cb, 0, 1, &scissor);

    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipeLayout, 0, 1, &descSets[currentFrame], 0, nullptr);

    // Draw all uploaded chunks
    for (auto& [pos, buf] : chunkBuffers) {
        if (buf.vertexCount == 0) continue;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &buf.vertexBuffer, &offset);
        vkCmdDraw(cb, buf.vertexCount, 1, 0, 0);
    }

    // Draw UI (hotbar + crosshair) — no depth test, on top of everything
    if (uiVertexBuf != VK_NULL_HANDLE && uiVertexCount > 0) {
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, uiPipeline);
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &uiVertexBuf, &offset);
        vkCmdDraw(cb, uiVertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(cb);
    if (vkEndCommandBuffer(cb) != VK_SUCCESS)
        throw std::runtime_error("vkEndCommandBuffer failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// Sync objects
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createSyncObjects() {
    imgAvail.resize(scImages.size());
    renderDone.resize(MAX_FRAMES);
    inFlight.resize(MAX_FRAMES);
    VkSemaphoreCreateInfo si{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i=0; i<scImages.size(); i++)
        if (vkCreateSemaphore(device, &si, nullptr, &imgAvail[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSemaphore (imgAvail) failed");
    for (int i=0; i<MAX_FRAMES; i++)
        if (vkCreateSemaphore(device, &si, nullptr, &renderDone[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fi, nullptr, &inFlight[i]) != VK_SUCCESS)
            throw std::runtime_error("vkCreate sync objects failed");
}

// ─────────────────────────────────────────────────────────────────────────────
// UI Pipeline — 2D ortho, no depth test, same atlas descriptor
// Vertex format: xy uv (4 floats)
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::createUIPipeline() {
    auto vert = readFile("shaders/ui_vert.spv");
    auto frag = readFile("shaders/ui_frag.spv");
    VkShaderModule vs = makeShaderModule(vert);
    VkShaderModule fs = makeShaderModule(frag);

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vs; stages[0].pName = "main";
    stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fs; stages[1].pName = "main";

    // xy uv — 4 floats
    VkVertexInputBindingDescription bind{0, 4*sizeof(float), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {
        {0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
        {1, 0, VK_FORMAT_R32G32_SFLOAT, 2*sizeof(float)}
    };
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount   = 1; vi.pVertexBindingDescriptions   = &bind;
    vi.vertexAttributeDescriptionCount = 2; vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo vs2{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vs2.viewportCount = 1; vs2.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode    = VK_CULL_MODE_NONE;
    rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth   = 1.f;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // No depth test — UI always on top
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    ds.depthTestEnable  = VK_FALSE;
    ds.depthWriteEnable = VK_FALSE;

    // Alpha blending for slot highlight
    VkPipelineColorBlendAttachmentState cba{};
    cba.blendEnable         = VK_TRUE;
    cba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    cba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    cba.colorBlendOp        = VK_BLEND_OP_ADD;
    cba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    cba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    cba.alphaBlendOp        = VK_BLEND_OP_ADD;
    cba.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|
                              VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cbs{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cbs.attachmentCount = 1; cbs.pAttachments = &cba;

    VkDynamicState dynStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount          = 2;   ci.pStages             = stages;
    ci.pVertexInputState   = &vi; ci.pInputAssemblyState = &ia;
    ci.pViewportState      = &vs2; ci.pRasterizationState = &rs;
    ci.pMultisampleState   = &ms; ci.pDepthStencilState  = &ds;
    ci.pColorBlendState    = &cbs; ci.pDynamicState       = &dyn;
    ci.layout              = pipeLayout; // reuse same layout — same descriptor set
    ci.renderPass          = renderPass;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &ci, nullptr, &uiPipeline) != VK_SUCCESS)
        throw std::runtime_error("createUIPipeline failed");

    vkDestroyShaderModule(device, vs, nullptr);
    vkDestroyShaderModule(device, fs, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain recreation
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::cleanupSwapchain() {
    vkDestroyImageView(device, depthView, nullptr);
    vkDestroyImage(device, depthImage, nullptr);
    vkFreeMemory(device, depthMemory, nullptr);
    for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
    for (auto iv : scViews) vkDestroyImageView(device, iv, nullptr);
    vkDestroySwapchainKHR(device, swapchain, nullptr);
}
void VulkanRenderer::recreateSwapchain() {
    int w=0, h=0;
    while (w==0 || h==0) { glfwGetFramebufferSize(window, &w, &h); glfwWaitEvents(); }
    vkDeviceWaitIdle(device);
    cleanupSwapchain();
    createSwapchain(); createImageViews(); createDepthResources(); createFramebuffers();
    imgAvailIdx = 0;
    // UI rebuild is done by App after this returns
}

// ─────────────────────────────────────────────────────────────────────────────
// Cleanup
// ─────────────────────────────────────────────────────────────────────────────
void VulkanRenderer::cleanup() {
    cleanupSwapchain();
    for (auto& [pos, buf] : chunkBuffers) {
        vkDestroyBuffer(device, buf.vertexBuffer, nullptr);
        vkFreeMemory(device, buf.vertexMemory, nullptr);
    }
    chunkBuffers.clear();
    for (int i=0; i<MAX_FRAMES; i++) {
        vkDestroyBuffer(device, ubos[i], nullptr);
        vkFreeMemory(device, uboMems[i], nullptr);
    }
    vkDestroyDescriptorPool(device, descPool, nullptr);
    vkDestroyDescriptorSetLayout(device, descLayout, nullptr);
    // Atlas
    vkDestroySampler(device, atlasSampler, nullptr);
    vkDestroyImageView(device, atlasView, nullptr);
    vkDestroyImage(device, atlasImage, nullptr);
    vkFreeMemory(device, atlasMemory, nullptr);
    vkDestroyPipeline(device, pipeline, nullptr);
    vkDestroyPipeline(device, pipelineWire, nullptr);
    vkDestroyPipeline(device, uiPipeline, nullptr);
    if (uiVertexBuf != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, uiVertexBuf, nullptr);
        vkFreeMemory(device, uiVertexMem, nullptr);
    }
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
    // NOTE: glfwDestroyWindow / glfwTerminate are handled by App
}
