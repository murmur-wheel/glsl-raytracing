//
// Created by murmur wheel on 2020/3/22.
//

#include "render.h"
#include <array>
#include <stack>
#include <unordered_map>

struct NodePack {
    uint32_t type;
    uint32_t object;
    uint32_t left;
    uint32_t right;
};

struct ScenePack {
    std::vector<NodePack> nodes;
    std::vector<uint32_t> integers;
    std::vector<glm::vec4> floats;
    std::vector<Short2> lights;

    void initialize(Node *root) {
        clear();

        std::unordered_map<Node *, uint32_t> node_id;
        std::unordered_map<Shape *, uint32_t> object_id;

        uint32_t id_alloc = 0;
        node_dfs_helper(id_alloc, node_id, root);

        nodes.resize(id_alloc);
        for (auto &p : node_id) {
            const auto id_at = [&node_id](Node *ptr) {
                return ptr ? node_id.at(ptr) : UINT32_MAX;
            };

            auto idx = node_id[p.first];
            nodes[idx].left = id_at(p.first->left);
            nodes[idx].right = id_at(p.first->right);
        }
    }

    static void node_dfs_helper(uint32_t &id_alloc,
        std::unordered_map<Node *, uint32_t> &node_id,
        Node *node) {
        node_id[node] = id_alloc++;
        if (node->left) {
            node_dfs_helper(id_alloc, node_id, node->left);
        }
        if (node->right) {
            node_dfs_helper(id_alloc, node_id, node->right);
        }
    }

private:
    void clear() {
        nodes.clear();
        integers.clear();
        floats.clear();
        lights.clear();
    }
};

void Render::initialize(Device *_device,
    SwapChain *_swap_chain,
    Scene *_scene,
    const Camera &_camera) {
    device = _device;
    swap_chain = _swap_chain;
    scene = _scene;

    sceneInitialize();
    traceInitialize();
    displayInitialize();

    VkFenceCreateInfo submitFenceInfo = {};
    submitFenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    MUST_SUCCESS(vkCreateFence(
        device->vk_device, &submitFenceInfo, nullptr, &submit_fence_));

    VkCommandPoolCreateInfo commandPoolCreateInfo = {};
    commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    commandPoolCreateInfo.queueFamilyIndex = device->graphics_queue_index;
    commandPoolCreateInfo.flags = 0;
    MUST_SUCCESS(vkCreateCommandPool(
        device->vk_device, &commandPoolCreateInfo, nullptr, &command_pool_));

    command_buffers_.resize(swap_chain->vk_images.size());
    VkCommandBufferAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = command_pool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount =
        static_cast<uint32_t>(swap_chain->vk_images.size());
    MUST_SUCCESS(vkAllocateCommandBuffers(
        device->vk_device, &allocateInfo, command_buffers_.data()));

    buildCommandBuffer();
}

void Render::finalize() {
    if (submit_fence_) {
        vkDestroyFence(device->vk_device, submit_fence_, nullptr);
    }
    if (command_pool_) {
        vkDestroyCommandPool(device->vk_device, command_pool_, nullptr);
    }

    displayFinalize();
    traceFinalize();
    sceneFinalize();
}

void Render::updateCamera(const Camera &camera) {}

void Render::drawFrame(uint32_t image_index) {
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &command_buffers_[image_index];

    VkQueue q = VK_NULL_HANDLE;
    vkGetDeviceQueue(device->vk_device, device->graphics_queue_index, 0, &q);

    MUST_SUCCESS(vkQueueSubmit(q, 1, &submitInfo, submit_fence_));
    MUST_SUCCESS(vkWaitForFences(
        device->vk_device, 1, &submit_fence_, VK_FALSE, UINT64_MAX));
    MUST_SUCCESS(vkResetFences(device->vk_device, 1, &submit_fence_));
}

void Render::sceneInitialize() {
    ScenePack pack_;
    // todo
    // pack_.initialize(scene->root);
}

void Render::sceneFinalize() {}

void Render::traceInitialize() {
    traceCreateDescriptorPool();
    traceCreateResultImage();
    traceCreatePipelineLayout();
    traceCreatePipeline();
}

void Render::traceFinalize() {
    vkDestroyPipeline(device->vk_device, trace_.pipeline, nullptr);
    vkDestroySampler(device->vk_device, trace_.resultImmutableSampler, nullptr);
    vkDestroyDescriptorSetLayout(
        device->vk_device, trace_.resultWriteSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(
        device->vk_device, trace_.resultReadSetLayout, nullptr);
    vkDestroyDescriptorPool(device->vk_device, trace_.descriptorPool, nullptr);
    vkDestroyPipelineLayout(device->vk_device, trace_.pipelineLayout, nullptr);
    device->destroyImage2D(&trace_.resultImage);
}

void Render::traceCreateDescriptorPool() {
    std::array<VkDescriptorPoolSize, 4> descriptorPoolSizes = {};
    descriptorPoolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorPoolSizes[0].descriptorCount = 1; // camera

    descriptorPoolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    descriptorPoolSizes[1].descriptorCount = 4;

    descriptorPoolSizes[2].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    descriptorPoolSizes[2].descriptorCount = 1;

    descriptorPoolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorPoolSizes[3].descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolCreateInfo = {};
    descriptorPoolCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolCreateInfo.maxSets = 3;
    descriptorPoolCreateInfo.poolSizeCount =
        static_cast<uint32_t>(descriptorPoolSizes.size());
    descriptorPoolCreateInfo.pPoolSizes = descriptorPoolSizes.data();
    MUST_SUCCESS(vkCreateDescriptorPool(device->vk_device,
        &descriptorPoolCreateInfo,
        nullptr,
        &trace_.descriptorPool));
}

void Render::traceCreateResultImage() {
    device->createImage2D(VK_FORMAT_R32G32B32A32_SFLOAT,
        swap_chain->image_extent,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        VK_IMAGE_ASPECT_COLOR_BIT,
        &trace_.resultImage);

    // create sampler
    VkSamplerCreateInfo samplerCreateInfo = {};
    samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerCreateInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerCreateInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    samplerCreateInfo.compareEnable = VK_FALSE;
    samplerCreateInfo.anisotropyEnable = VK_FALSE;
    samplerCreateInfo.magFilter = VK_FILTER_LINEAR;
    samplerCreateInfo.minFilter = VK_FILTER_NEAREST;
    MUST_SUCCESS(vkCreateSampler(device->vk_device,
        &samplerCreateInfo,
        nullptr,
        &trace_.resultImmutableSampler));

    // create descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 1> resultBindings = {};
    resultBindings[0].descriptorCount = 1;
    resultBindings[0].binding = 0;
    resultBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    resultBindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
    descriptorSetLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutCreateInfo.bindingCount = 1;
    descriptorSetLayoutCreateInfo.pBindings = resultBindings.data();
    MUST_SUCCESS(vkCreateDescriptorSetLayout(device->vk_device,
        &descriptorSetLayoutCreateInfo,
        nullptr,
        &trace_.resultWriteSetLayout));

    // allocate descriptor set
    VkDescriptorSetAllocateInfo writeSetAllocateInfo = {};
    writeSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    writeSetAllocateInfo.descriptorPool = trace_.descriptorPool;
    writeSetAllocateInfo.descriptorSetCount = 1;
    writeSetAllocateInfo.pSetLayouts = &trace_.resultWriteSetLayout;
    MUST_SUCCESS(vkAllocateDescriptorSets(
        device->vk_device, &writeSetAllocateInfo, &trace_.resultWriteSet));

    // write descriptor
    VkDescriptorImageInfo storageImageDescriptor = {};
    storageImageDescriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageImageDescriptor.imageView = trace_.resultImage.imageView;
    storageImageDescriptor.sampler = VK_NULL_HANDLE;
    traceWriteImageDescriptor(trace_.resultWriteSet,
        0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        &storageImageDescriptor);

    // create read setLayout
    VkDescriptorSetLayoutBinding readSetLayoutBinding = {};
    readSetLayoutBinding.descriptorCount = 1;
    readSetLayoutBinding.descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    readSetLayoutBinding.binding = 0;
    readSetLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    readSetLayoutBinding.pImmutableSamplers = &trace_.resultImmutableSampler;

    VkDescriptorSetLayoutCreateInfo readSetLayoutCreateInfo = {};
    readSetLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    readSetLayoutCreateInfo.bindingCount = 1;
    readSetLayoutCreateInfo.pBindings = &readSetLayoutBinding;
    MUST_SUCCESS(vkCreateDescriptorSetLayout(device->vk_device,
        &readSetLayoutCreateInfo,
        nullptr,
        &trace_.resultReadSetLayout));

    // allocate read set
    VkDescriptorSetAllocateInfo readSetAllocateInfo = {};
    readSetAllocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    readSetAllocateInfo.descriptorPool = trace_.descriptorPool;
    readSetAllocateInfo.descriptorSetCount = 1;
    readSetAllocateInfo.pSetLayouts = &trace_.resultReadSetLayout;
    MUST_SUCCESS(vkAllocateDescriptorSets(
        device->vk_device, &readSetAllocateInfo, &trace_.resultReadSet));

    // write to sampler
    VkDescriptorImageInfo sampledImageDescriptor = {};
    sampledImageDescriptor.sampler = VK_NULL_HANDLE;
    sampledImageDescriptor.imageView = trace_.resultImage.imageView;
    sampledImageDescriptor.imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    traceWriteImageDescriptor(trace_.resultReadSet,
        0,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        &sampledImageDescriptor);
}

void Render::traceCreatePipelineLayout() {
    std::array<VkDescriptorSetLayout, 1> setLayouts = {};
    setLayouts[0] = trace_.resultWriteSetLayout;

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount =
        static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutCreateInfo.pSetLayouts = setLayouts.data();

    MUST_SUCCESS(vkCreatePipelineLayout(device->vk_device,
        &pipelineLayoutCreateInfo,
        nullptr,
        &trace_.pipelineLayout));
}

void Render::traceCreatePipeline() {
    VkPipelineShaderStageCreateInfo shaderStageCreateInfo = {};
    shaderStageCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageCreateInfo.module =
        device->loadShaderModule("res/trace.comp.spv");
    shaderStageCreateInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageCreateInfo.pName = "main";

    VkComputePipelineCreateInfo computePipelineCreateInfo = {};
    computePipelineCreateInfo.sType =
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    computePipelineCreateInfo.layout = trace_.pipelineLayout;
    computePipelineCreateInfo.stage = shaderStageCreateInfo;
    MUST_SUCCESS(vkCreateComputePipelines(device->vk_device,
        VK_NULL_HANDLE,
        1,
        &computePipelineCreateInfo,
        nullptr,
        &trace_.pipeline));

    vkDestroyShaderModule(
        device->vk_device, shaderStageCreateInfo.module, nullptr);
}

void Render::traceUpdateResultImageLayout(VkCommandBuffer commandBuffer,
    VkAccessFlags srcAccessFlags,
    VkAccessFlags dstAccessFlags,
    VkImageLayout oldLayout,
    VkImageLayout newLayout) {
    VkImageMemoryBarrier imageMemoryBarrier = {};
    imageMemoryBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageMemoryBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageMemoryBarrier.srcAccessMask = srcAccessFlags;
    imageMemoryBarrier.dstAccessMask = dstAccessFlags;
    imageMemoryBarrier.oldLayout = oldLayout;
    imageMemoryBarrier.newLayout = newLayout;
    imageMemoryBarrier.image = trace_.resultImage.image;
    imageMemoryBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageMemoryBarrier.subresourceRange.baseMipLevel = 0;
    imageMemoryBarrier.subresourceRange.levelCount = 1;
    imageMemoryBarrier.subresourceRange.baseArrayLayer = 0;
    imageMemoryBarrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(commandBuffer,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,
        0,
        nullptr,
        0,
        nullptr,
        1,
        &imageMemoryBarrier);
}

void Render::traceWriteImageDescriptor(VkDescriptorSet set,
    uint32_t dstBinding,
    VkDescriptorType descriptorType,
    const VkDescriptorImageInfo *imageInfo) {
    VkWriteDescriptorSet writeDescriptorSet = {};
    writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writeDescriptorSet.descriptorType = descriptorType;
    writeDescriptorSet.dstSet = set;
    writeDescriptorSet.dstBinding = dstBinding;
    writeDescriptorSet.dstArrayElement = 0;
    writeDescriptorSet.descriptorCount = 1;
    writeDescriptorSet.pImageInfo = imageInfo;
    vkUpdateDescriptorSets(
        device->vk_device, 1, &writeDescriptorSet, 0, nullptr);
}

void Render::traceDispatch(VkCommandBuffer commandBuffer) {
    // update resultImage Layout
    traceUpdateResultImageLayout(commandBuffer,
        VK_ACCESS_MEMORY_READ_BIT,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_GENERAL);

    // bind resources
    // bind compute pipeline
    vkCmdBindDescriptorSets(commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        trace_.pipelineLayout,
        0,
        1,
        &trace_.resultWriteSet,
        0,
        nullptr);
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, trace_.pipeline);
    vkCmdDispatch(commandBuffer,
        swap_chain->image_extent.width / 2,
        swap_chain->image_extent.height / 2,
        1);

    // update resultImage layout
    traceUpdateResultImageLayout(commandBuffer,
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_READ_BIT,
        VK_IMAGE_LAYOUT_GENERAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Render::displayInitialize() {
    displayCreateDescriptorPool();
    displayCreateTimesUniformBuffer();
    displayCreateRenderPass();
    displayCreateFramebuffers();
    displayCreatePipelineLayout();
    displayCreatePipeline();
}

void Render::displayFinalize() {
    // timesUniformBuffer
    vkDestroyDescriptorPool(
        device->vk_device, display_.timesUniformBuffer.descriptorPool, nullptr);

    // pipeline
    vkDestroyDescriptorPool(
        device->vk_device, display_.descriptorPool, nullptr);
    vkDestroyPipeline(device->vk_device, display_.pipeline, nullptr);
    vkDestroyPipelineLayout(
        device->vk_device, display_.pipelineLayout, nullptr);
    for (auto &f : display_.framebuffers) {
        vkDestroyFramebuffer(device->vk_device, f, nullptr);
    }
    if (display_.renderPass) {
        vkDestroyRenderPass(device->vk_device, display_.renderPass, nullptr);
    }
}

void Render::displayCreateDescriptorPool() {
    std::array<VkDescriptorPoolSize, 1> poolSizes = {};
    poolSizes[0].descriptorCount = 1;
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;

    VkDescriptorPoolCreateInfo poolCreateInfo = {};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolCreateInfo.maxSets = 1;
    poolCreateInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolCreateInfo.pPoolSizes = poolSizes.data();
    (vkCreateDescriptorPool(
        device->vk_device, &poolCreateInfo, nullptr, &display_.descriptorPool));
}

void Render::displayCreateTimesUniformBuffer() {
    // create buffer
    std::array<VkDescriptorPoolSize, 1> poolSizes = {};
    poolSizes[0].descriptorCount = 1;
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    display_.timesUniformBuffer.descriptorPool =
        device->createDescriptorPool(poolSizes, 1);
}

void Render::displayCreateRenderPass() {
    std::array<VkAttachmentDescription, 1> attachmentDescriptions = {};
    // color buffer
    attachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachmentDescriptions[0].format = swap_chain->image_format;
    attachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

    VkAttachmentReference colorReference = {
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
    };

    VkSubpassDescription subpassDescription = {};
    subpassDescription.colorAttachmentCount = 1;
    subpassDescription.pColorAttachments = &colorReference;

    std::array<VkSubpassDependency, 2> subpassDependencies = {};
    subpassDependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    subpassDependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[0].dstSubpass = 0;
    subpassDependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    subpassDependencies[1].srcSubpass = 0;
    subpassDependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    subpassDependencies[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    subpassDependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    subpassDependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    subpassDependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    subpassDependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassCreateInfo = {};
    renderPassCreateInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassCreateInfo.attachmentCount = 1;
    renderPassCreateInfo.pAttachments = attachmentDescriptions.data();
    renderPassCreateInfo.subpassCount = 1;
    renderPassCreateInfo.pSubpasses = &subpassDescription;
    renderPassCreateInfo.dependencyCount = 2;
    renderPassCreateInfo.pDependencies = subpassDependencies.data();
    MUST_SUCCESS(vkCreateRenderPass(device->vk_device,
        &renderPassCreateInfo,
        nullptr,
        &display_.renderPass));
}

void Render::displayCreateFramebuffers() {
    display_.framebuffers.resize(swap_chain->vk_images.size());
    for (size_t i = 0; i < display_.framebuffers.size(); ++i) {
        VkFramebufferCreateInfo framebufferCreateInfo = {};
        framebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferCreateInfo.renderPass = display_.renderPass;
        framebufferCreateInfo.attachmentCount = 1;
        framebufferCreateInfo.pAttachments = &swap_chain->vk_image_views[i];
        framebufferCreateInfo.width = swap_chain->image_extent.width;
        framebufferCreateInfo.height = swap_chain->image_extent.height;
        framebufferCreateInfo.layers = 1;
        MUST_SUCCESS(vkCreateFramebuffer(device->vk_device,
            &framebufferCreateInfo,
            nullptr,
            &display_.framebuffers[i]));
    }
}

void Render::displayCreatePipelineLayout() {
    std::array<VkDescriptorSetLayout, 1> setLayouts = {};
    setLayouts[0] = trace_.resultReadSetLayout;

    VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
    pipelineLayoutCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutCreateInfo.setLayoutCount =
        static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutCreateInfo.pSetLayouts = setLayouts.data();

    MUST_SUCCESS(vkCreatePipelineLayout(device->vk_device,
        &pipelineLayoutCreateInfo,
        nullptr,
        &display_.pipelineLayout));
}

void Render::displayCreatePipeline() {
    std::vector<std::pair<VkShaderModule, VkShaderStageFlagBits>>
        shaderStages = {
            {
                device->loadShaderModule("res/display.vert.spv"),
                VK_SHADER_STAGE_VERTEX_BIT,
            },
            {
                device->loadShaderModule("res/display.frag.spv"),
                VK_SHADER_STAGE_FRAGMENT_BIT,
            },
        };
    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStageCreateInfos = {};
    for (int i = 0; i < 2; ++i) {
        shaderStageCreateInfos[i].sType =
            VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageCreateInfos[i].module = shaderStages[i].first;
        shaderStageCreateInfos[i].stage = shaderStages[i].second;
        shaderStageCreateInfos[i].pName = "main";
    }

    VkPipelineVertexInputStateCreateInfo vertexInputStateCreateInfo = {};
    vertexInputStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo = {};
    inputAssemblyStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo = {};
    rasterizationStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
    rasterizationStateCreateInfo.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo = {};
    multisampleStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineViewportStateCreateInfo viewportStateCreateInfo = {};
    viewportStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportStateCreateInfo.scissorCount = 1;
    viewportStateCreateInfo.viewportCount = 1;

    std::array<VkPipelineColorBlendAttachmentState, 1> attachmentStates = {};
    attachmentStates[0].colorWriteMask = 0x0f;
    attachmentStates[0].blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo blendStateCreateInfo = {};
    blendStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blendStateCreateInfo.attachmentCount = 1;
    blendStateCreateInfo.pAttachments = &attachmentStates[0];

    std::array<VkDynamicState, 2> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo = {};
    dynamicStateCreateInfo.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateCreateInfo.dynamicStateCount = 2;
    dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo = {};
    graphicsPipelineCreateInfo.sType =
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    graphicsPipelineCreateInfo.stageCount = 2;
    graphicsPipelineCreateInfo.pStages = shaderStageCreateInfos.data();

    graphicsPipelineCreateInfo.pVertexInputState = &vertexInputStateCreateInfo;
    graphicsPipelineCreateInfo.pInputAssemblyState =
        &inputAssemblyStateCreateInfo;
    graphicsPipelineCreateInfo.pRasterizationState =
        &rasterizationStateCreateInfo;
    graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
    graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
    graphicsPipelineCreateInfo.pColorBlendState = &blendStateCreateInfo;
    graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
    graphicsPipelineCreateInfo.renderPass = display_.renderPass;
    graphicsPipelineCreateInfo.layout = display_.pipelineLayout;

    MUST_SUCCESS(vkCreateGraphicsPipelines(device->vk_device,
        VK_NULL_HANDLE,
        1,
        &graphicsPipelineCreateInfo,
        nullptr,
        &display_.pipeline));

    for (auto &s : shaderStages) {
        vkDestroyShaderModule(device->vk_device, s.first, nullptr);
    }
}

void Render::displayDraw(VkCommandBuffer commandBuffer, uint32_t imageIndex) {
    std::array<VkClearValue, 1> clear_values = {};
    clear_values[0].color.float32[0] = 1.0f;
    clear_values[0].color.float32[1] = 0.0f;
    clear_values[0].color.float32[2] = 0.0f;
    clear_values[0].color.float32[3] = 1.0f;

    VkRenderPassBeginInfo renderPassBeginInfo = {};
    renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBeginInfo.renderPass = display_.renderPass;
    renderPassBeginInfo.framebuffer = display_.framebuffers[imageIndex];
    renderPassBeginInfo.clearValueCount = 1;
    renderPassBeginInfo.pClearValues = clear_values.data();
    renderPassBeginInfo.renderArea.offset = {};
    renderPassBeginInfo.renderArea.extent = swap_chain->image_extent;
    vkCmdBeginRenderPass(
        commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport = {};
    viewport.x = 0;
    viewport.y = 0;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    viewport.width = float(swap_chain->image_extent.width);
    viewport.height = float(swap_chain->image_extent.height);
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &renderPassBeginInfo.renderArea);

    vkCmdBindDescriptorSets(commandBuffer,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        display_.pipelineLayout,
        0,
        1,
        &trace_.resultReadSet,
        0,
        nullptr);
    vkCmdBindPipeline(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, display_.pipeline);
    vkCmdDraw(commandBuffer, 6, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
}

void Render::buildCommandBuffer() {
    for (size_t i = 0; i < command_buffers_.size(); ++i) {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        MUST_SUCCESS(vkBeginCommandBuffer(command_buffers_[i], &beginInfo));

        traceDispatch(command_buffers_[i]);
        displayDraw(command_buffers_[i], uint32_t(i));

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.image = swap_chain->vk_images[i];
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        vkCmdPipelineBarrier(command_buffers_[i],
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &barrier);

        MUST_SUCCESS(vkEndCommandBuffer(command_buffers_[i]));
    }
}