#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cglm/cglm.h>

#include <array>
#include <optional>
#include <string>
#include <vector>

static constexpr int MAX_FRAMES = 2;

struct UBO {
    mat4 mvp;
};

class App {
public:
    void run();

private:
    // ── window ────────────────────────────────────────────────────────────
    GLFWwindow* window = nullptr;

    // ── core Vulkan ───────────────────────────────────────────────────────
    VkInstance               instance       = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         physDev        = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;

    // ── swapchain ─────────────────────────────────────────────────────────
    VkSwapchainKHR             swapchain = VK_NULL_HANDLE;
    std::vector<VkImage>       scImages;
    std::vector<VkImageView>   scViews;
    VkFormat                   scFormat{};
    VkExtent2D                 scExtent{};

    // ── render pass & pipeline ────────────────────────────────────────────
    VkRenderPass          renderPass   = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout   = VK_NULL_HANDLE;
    VkPipelineLayout      pipeLayout   = VK_NULL_HANDLE;
    VkPipeline            pipeline     = VK_NULL_HANDLE;
    VkPipeline            pipelineWire = VK_NULL_HANDLE;

    // ── depth ─────────────────────────────────────────────────────────────
    VkImage        depthImage  = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory = VK_NULL_HANDLE;
    VkImageView    depthView   = VK_NULL_HANDLE;

    // ── framebuffers ──────────────────────────────────────────────────────
    std::vector<VkFramebuffer> framebuffers;

    // ── vertex buffer ─────────────────────────────────────────────────────
    VkBuffer       vertexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
    uint32_t       vertexCount  = 0;

    // ── uniform buffers ───────────────────────────────────────────────────
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
    uint32_t                     currentFrame  = 0;
    uint32_t                     imgAvailIdx   = 0;

    bool framebufferResized = false;

    // ── internal helpers ──────────────────────────────────────────────────
    struct QueueFamilies {
        std::optional<uint32_t> graphics, present;
        bool complete() const { return graphics.has_value() && present.has_value(); }
    };

    void initWindow();
    void initVulkan();
    void mainLoop();
    void drawFrame();
    void cleanup();

    void createInstance();
    void setupDebugMessenger();
    void createSurface();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createDescriptorSetLayout();
    void createGraphicsPipelines();
    void createDepthResources();
    void createFramebuffers();
    void createCommandPool();
    void createVertexBuffer();
    void createUniformBuffers();
    void createDescriptorPool();
    void createDescriptorSets();
    void createCommandBuffers();
    void createSyncObjects();
    void updateUniformBuffer(uint32_t frame);
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx);
    void cleanupSwapchain();
    void recreateSwapchain();

    QueueFamilies   findQueueFamilies(VkPhysicalDevice dev);
    bool            deviceSuitable(VkPhysicalDevice dev);
    bool            checkValidationSupport();
    std::vector<const char*> getRequiredExtensions();

    VkSurfaceFormatKHR chooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& fmts);
    VkPresentModeKHR   choosePresentMode(const std::vector<VkPresentModeKHR>& modes);
    VkExtent2D         chooseExtent(const VkSurfaceCapabilitiesKHR& caps);

    VkImageView    makeImageView(VkImage img, VkFormat fmt, VkImageAspectFlags aspect);
    VkShaderModule makeShaderModule(const std::vector<char>& code);

    void     createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                          VkMemoryPropertyFlags props,
                          VkBuffer& buf, VkDeviceMemory& mem);
    void     copyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size);
    void     createImage(uint32_t w, uint32_t h, VkFormat fmt,
                         VkImageUsageFlags usage,
                         VkImage& img, VkDeviceMemory& mem);
    uint32_t findMemType(uint32_t filter, VkMemoryPropertyFlags props);
};
