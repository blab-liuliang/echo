#include "d3d11_shader_program.h"
#include "d3d11_renderer.h"
#include "d3d11_mapping.h"
#include "base/glslcc/glsl_cross_compiler.h"

namespace Echo
{
	static bool createShader(const vector<ui32>::type& spirv, VkShaderModule& vkShader, spirv_cross::Compiler*& shaderCompiler)
	{
        if (!spirv.empty())
        {
            VKRenderer* vkRenderer = ECHO_DOWN_CAST<VKRenderer*>(Renderer::instance());

            VkShaderModuleCreateInfo createInfo;
            createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            createInfo.pNext = nullptr;
            createInfo.flags = 0;
            createInfo.codeSize = spirv.size() * sizeof(ui32);
            createInfo.pCode = spirv.data();

            // reflect
            shaderCompiler = EchoNew(spirv_cross::Compiler(spirv));

            if (VK_SUCCESS == vkCreateShaderModule(vkRenderer->getVkDevice(), &createInfo, nullptr, &vkShader))
                return true;
        }

        EchoLogError("Vulkan create shader failed");
        return false;
    }

    D3D11ShaderProgram::~D3D11ShaderProgram()
    {
        VKRenderer* vkRenderer = ECHO_DOWN_CAST<VKRenderer*>(Renderer::instance());

        vkDestroyShaderModule(vkRenderer->getVkDevice(), m_vkVertexShader, nullptr);
        vkDestroyShaderModule(vkRenderer->getVkDevice(), m_vkFragmentShader, nullptr);
    }

    bool D3D11ShaderProgram::createShaderProgram(const String& vsSrc, const String& psSrc)
    {
        GLSLCrossCompiler glslCompiler;
        glslCompiler.setInput(vsSrc.c_str(), psSrc.c_str(), nullptr);

        bool isCreateVSSucceed = createShader(glslCompiler.getSPIRV(GLSLCrossCompiler::ShaderType::VS), m_vkVertexShader, m_vertexShaderCompiler);
        bool isCreateFSSucceed = createShader(glslCompiler.getSPIRV(GLSLCrossCompiler::ShaderType::FS), m_vkFragmentShader, m_fragmentShaderCompiler);
        m_isLinked = isCreateVSSucceed && isCreateFSSucceed;

        // create shader stage
        if (m_isLinked)
        {
            m_vkShaderStagesCreateInfo.assign({});
            m_vkShaderStagesCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            m_vkShaderStagesCreateInfo[0].flags = 0;
            m_vkShaderStagesCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            m_vkShaderStagesCreateInfo[0].module = m_vkVertexShader;
            m_vkShaderStagesCreateInfo[0].pName = "main";

            m_vkShaderStagesCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            m_vkShaderStagesCreateInfo[1].flags = 0;
            m_vkShaderStagesCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            m_vkShaderStagesCreateInfo[1].module = m_vkFragmentShader;
            m_vkShaderStagesCreateInfo[1].pName = "main";

            if (parseUniforms())
            {
				createVkDescriptorSetLayout();
                createVkPipelineLayout();
            }
        }

        return m_isLinked;
    }

    void D3D11ShaderProgram::createVkUniformBuffer(UniformsInstance& uniformsInstance)
    {
        // this is not good here, reuse VkBuffer afterwards
        EchoSafeDelete(uniformsInstance.m_vkVertexShaderUniformBuffer, VKBuffer);
        EchoSafeDelete(uniformsInstance.m_vkFragmentShaderUniformBuffer, VKBuffer);

        Buffer vertUniformBuffer(m_vertexShaderUniformBytes.size(), m_vertexShaderUniformBytes.data(), false);
        uniformsInstance.m_vkVertexShaderUniformBuffer = EchoNew(VKBuffer(GPUBuffer::GPUBufferType::GBT_UNIFORM, GPUBuffer::GBU_DYNAMIC, vertUniformBuffer));

        Buffer fragmentUniformBuffer(m_fragmentShaderUniformBytes.size(), m_fragmentShaderUniformBytes.data(), false);
        uniformsInstance.m_vkFragmentShaderUniformBuffer = EchoNew(VKBuffer(GPUBuffer::GPUBufferType::GBT_UNIFORM, GPUBuffer::GBU_DYNAMIC, fragmentUniformBuffer));

        // Store information in the uniform's descriptor that is used by the descriptor set
        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::VS].buffer = uniformsInstance.m_vkVertexShaderUniformBuffer->getVkBuffer();
        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::VS].offset = 0;
        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::VS].range = uniformsInstance.m_vkVertexShaderUniformBuffer->getSize();

        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::FS].buffer = uniformsInstance.m_vkFragmentShaderUniformBuffer->getVkBuffer();
        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::FS].offset = 0;
        uniformsInstance.m_vkShaderUniformBufferDescriptors[ShaderType::FS].range = uniformsInstance.m_vkFragmentShaderUniformBuffer->getSize();
    }

    void D3D11ShaderProgram::updateVkUniformBuffer(UniformsInstance& uniformsInstance)
    {
        if (!m_isLinked) return;

        for (UniformMap& uniformMap : m_uniforms)
        {
            // organize uniform bytes
            for (UniformMap::iterator it = uniformMap.begin(); it != uniformMap.end(); it++)
            {
                UniformPtr uniform = it->second;
                void* value = uniform->m_value.empty() ? uniform->getValueDefault().data() : uniform->m_value.data();
                if (value && uniform->m_type != SPT_UNKNOWN)
                {
                    vector<Byte>::type& uniformBytes = uniform->m_shader == ShaderType::VS ? m_vertexShaderUniformBytes : m_fragmentShaderUniformBytes;
                    if (uniform->m_type != SPT_TEXTURE)
                    {
                        std::memcpy(uniformBytes.data() + uniform->m_location, value, uniform->m_sizeInBytes * sizeof(Byte));
                    }
                    else
                    {

                    }
                }
            }
        }

        if (!uniformsInstance.m_vkVertexShaderUniformBuffer ||
            !uniformsInstance.m_vkFragmentShaderUniformBuffer ||
            uniformsInstance.m_vkVertexShaderUniformBuffer->getSize() != m_vertexShaderUniformBytes.size() ||
            uniformsInstance.m_vkFragmentShaderUniformBuffer->getSize() != m_fragmentShaderUniformBytes.size())
        {
            createVkUniformBuffer(uniformsInstance);
            createVkDescriptorSet(uniformsInstance);
        }

        Buffer vertUniformBuffer(m_vertexShaderUniformBytes.size(), m_vertexShaderUniformBytes.data(), false);
        uniformsInstance.m_vkVertexShaderUniformBuffer->updateData(vertUniformBuffer);

        Buffer fragmentUniformBuffer(m_fragmentShaderUniformBytes.size(), m_fragmentShaderUniformBytes.data(), false);
        uniformsInstance.m_vkFragmentShaderUniformBuffer->updateData( fragmentUniformBuffer);
    }

    void D3D11ShaderProgram::createVkDescriptorSet(UniformsInstance& uniformsInstance)
    {
        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool =VKRenderer::instance()->getVkDescriptorPool();
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &m_vkDescriptorSetLayout;

        VkResult result = vkAllocateDescriptorSets(VKRenderer::instance()->getVkDevice(), &allocInfo, &uniformsInstance.m_vkDescriptorSet);
        if (VK_SUCCESS != result)
        {
            EchoLogError("vulkan set descriptor set failed.");
        }
    }

    void D3D11ShaderProgram::updateDescriptorSet(UniformsInstance& uniformsInstance)
    {
        // Update the descriptor set determining the shader binding points
        // For every binding point used in a shader there needs to be one
        // descriptor set matching that binding point
        vector<VkWriteDescriptorSet>::type writeDescriptorSets;
        for (size_t i = 0; i < uniformsInstance.m_vkShaderUniformBufferDescriptors.size(); i++)
        {
            if (uniformsInstance.m_vkShaderUniformBufferDescriptors[i].buffer)
            {
                // Binding 0 : Uniform buffer
                VkWriteDescriptorSet writeDescriptorSet;
                writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writeDescriptorSet.pNext = nullptr;
                writeDescriptorSet.dstSet = uniformsInstance.m_vkDescriptorSet;
                writeDescriptorSet.dstBinding = i;
                writeDescriptorSet.dstArrayElement = 0;
                writeDescriptorSet.descriptorCount = 1;
                writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
                writeDescriptorSet.pImageInfo = nullptr;
                writeDescriptorSet.pBufferInfo = &uniformsInstance.m_vkShaderUniformBufferDescriptors[i];
                writeDescriptorSet.pTexelBufferView = nullptr;

                writeDescriptorSets.emplace_back(writeDescriptorSet);
            }
        }

        for (UniformMap& uniformMap : m_uniforms)
        {
            for (auto& it : uniformMap)
            {
                UniformPtr& uniform = it.second;
                if (uniform->m_type == SPT_TEXTURE)
                {
                    i32 textureIdx = *(i32*)(uniform->getValue().data());
                    VKTexture* texture = VKRenderer::instance()->getTexture(textureIdx);
                    if (texture && texture->getVkDescriptorImageInfo())
                    {
                        VkWriteDescriptorSet writeDescriptorSet;
                        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writeDescriptorSet.pNext = nullptr;
                        writeDescriptorSet.dstSet = uniformsInstance.m_vkDescriptorSet;
                        writeDescriptorSet.dstBinding = uniform->m_location;
                        writeDescriptorSet.dstArrayElement = 0;
                        writeDescriptorSet.descriptorCount = 1;
                        writeDescriptorSet.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        writeDescriptorSet.pImageInfo = texture->getVkDescriptorImageInfo();
                        writeDescriptorSet.pBufferInfo = nullptr;
                        writeDescriptorSet.pTexelBufferView = nullptr;

                        writeDescriptorSets.emplace_back(writeDescriptorSet);
                    }
                    else
                    {
                        EchoLogError("vulkan write descriptor set have empty image info.");
                    }
                }
            }
        }

        vkUpdateDescriptorSets(VKRenderer::instance()->getVkDevice(), writeDescriptorSets.size(), writeDescriptorSets.data(), 0, nullptr);
    }

    void D3D11ShaderProgram::createVkDescriptorSetLayout()
    {
        auto isHaveNormalUniform = [](UniformMap& uniformMap)->bool
        {
            for (auto& it : uniformMap)
            {
                if (it.second->m_type != SPT_TEXTURE)
                    return true;
            }

            return false;
        };

        m_layoutBindings.clear();

        if (isHaveNormalUniform(m_uniforms[ShaderType::VS]))
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding;
            uboLayoutBinding.binding = 0;
            uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboLayoutBinding.descriptorCount = 1;
            uboLayoutBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
            uboLayoutBinding.pImmutableSamplers = nullptr;
            m_layoutBindings.emplace_back(uboLayoutBinding);
        }

        if (isHaveNormalUniform(m_uniforms[ShaderType::FS]))
        {
            VkDescriptorSetLayoutBinding uboLayoutBinding;
            uboLayoutBinding.binding = 1;
            uboLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            uboLayoutBinding.descriptorCount = 1;
            uboLayoutBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            uboLayoutBinding.pImmutableSamplers = nullptr;
            m_layoutBindings.emplace_back(uboLayoutBinding);
        }

        for (UniformMap& uniformMap : m_uniforms)
        {
            for (auto& it : uniformMap)
            {
                UniformPtr& uniform = it.second;
                if (uniform->m_type == SPT_TEXTURE)
                {
                    VkDescriptorSetLayoutBinding samplerLayoutBinding;
                    samplerLayoutBinding.binding = it.second->m_location;
                    samplerLayoutBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                    samplerLayoutBinding.descriptorCount = 1;
                    samplerLayoutBinding.stageFlags = (uniform->m_shader == ShaderType::VS) ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;;
                    samplerLayoutBinding.pImmutableSamplers = nullptr;
                    m_layoutBindings.emplace_back(samplerLayoutBinding);
                }
            }
        }


		// create a descriptor set layout based on layout bindings
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo = {};
        descriptorSetLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutCreateInfo.pNext = nullptr;
        descriptorSetLayoutCreateInfo.flags = 0;
        descriptorSetLayoutCreateInfo.bindingCount = m_layoutBindings.size();
        descriptorSetLayoutCreateInfo.pBindings = m_layoutBindings.data();

		VKDebug(vkCreateDescriptorSetLayout(VKRenderer::instance()->getVkDevice(), &descriptorSetLayoutCreateInfo, nullptr, &m_vkDescriptorSetLayout));
    }

    // https://vulkan.lunarg.com/doc/view/1.2.162.1/mac/tutorial/html/08-init_pipeline_layout.html
    void D3D11ShaderProgram::createVkPipelineLayout()
    {
        VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = {};
        pipelineLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipelineLayoutCreateInfo.pNext = nullptr;
        pipelineLayoutCreateInfo.flags = 0;
        pipelineLayoutCreateInfo.setLayoutCount = 1;
        pipelineLayoutCreateInfo.pSetLayouts = &m_vkDescriptorSetLayout;
        pipelineLayoutCreateInfo.pushConstantRangeCount = 0;
        pipelineLayoutCreateInfo.pPushConstantRanges = nullptr;

        VKDebug(vkCreatePipelineLayout(VKRenderer::instance()->getVkDevice(), &pipelineLayoutCreateInfo, nullptr, &m_vkPipelineLayout));
    }

    // https://www.khronos.org/assets/uploads/developers/library/2016-vulkan-devday-uk/4-Using-spir-v-with-spirv-cross.pdf
    bool D3D11ShaderProgram::parseUniforms()
    {
        for (ShaderProgram::UniformMap& uniformMap : m_uniforms)
            uniformMap.clear();

        // vertex uniforms
        SPIRV_CROSS_NAMESPACE::ShaderResources vsResources = m_vertexShaderCompiler->get_shader_resources();
        {
			for (SPIRV_CROSS_NAMESPACE::Resource& resource : vsResources.uniform_buffers)
				addUniform(resource, ShaderType::VS);
        }

        // fragment uniforms
        SPIRV_CROSS_NAMESPACE::ShaderResources fsResources = m_fragmentShaderCompiler->get_shader_resources();
        {
			for (SPIRV_CROSS_NAMESPACE::Resource& resource : fsResources.uniform_buffers)
				addUniform(resource, ShaderType::FS);

			for (SPIRV_CROSS_NAMESPACE::Resource& resource : fsResources.sampled_images)
				addUniform(resource, ShaderType::FS);
        }

        allocUniformBytes();

        return !m_uniforms.empty();
    }

    void D3D11ShaderProgram::addUniform(spirv_cross::Resource& resource, ShaderType shaderType)
    {
        spirv_cross::Compiler* compiler = shaderType == ShaderType::VS ? m_vertexShaderCompiler : m_fragmentShaderCompiler;
        const spirv_cross::SPIRType& type = compiler->get_type(resource.base_type_id);
        if (type.basetype == spirv_cross::SPIRType::SampledImage)
        {
			Uniform* desc = EchoNew(UniformTexture);
			desc->m_name = resource.name.c_str();
			desc->m_shader = shaderType;
			desc->m_type = VKMapping::mapUniformType(type);
			desc->m_count = 1;
            desc->m_sizeInBytes = 4;
            desc->m_location = compiler->get_decoration(resource.id, spv::DecorationBinding);
			m_uniforms[shaderType][desc->m_name] = desc;
        }
        else
        {
			size_t memberCount = type.member_types.size();
			for (size_t i = 0; i < memberCount; i++)
			{
				Uniform* desc = EchoNew(UniformNormal);
				desc->m_name = compiler->get_member_name(type.self, i);
				desc->m_shader = shaderType;
				desc->m_sizeInBytes = compiler->get_declared_struct_member_size(type, i);
				desc->m_type = VKMapping::mapUniformType(compiler->get_type(type.member_types[i]));
				desc->m_count = desc->m_sizeInBytes / mapUniformTypeSize(desc->m_type);
				desc->m_location = compiler->type_struct_member_offset(type, i);
				m_uniforms[shaderType][desc->m_name] = desc;
			}
        }
    }

    void D3D11ShaderProgram::allocUniformBytes()
    {
        m_vertexShaderUniformBytes.clear();
        m_fragmentShaderUniformBytes.clear();

        for (ShaderProgram::UniformMap& uniformMap : m_uniforms)
        {
            for (auto& it : uniformMap)
            {
                UniformPtr uniform = it.second;
                vector<Byte>::type& uniformBytes = uniform->m_shader == ShaderType::VS ? m_vertexShaderUniformBytes : m_fragmentShaderUniformBytes;
                i32 bytes = uniform->m_location + uniform->m_sizeInBytes;
                while (uniformBytes.size() < bytes)
                {
                    uniformBytes.emplace_back(0);
                }
            }
        }
    }

    void D3D11ShaderProgram::bindUniforms(VkCommandBuffer& vkCommandbuffer, UniformsInstance& uniformsInstance)
    {
        // update uniform VkBuffer by memory
        updateVkUniformBuffer(uniformsInstance);
        updateDescriptorSet(uniformsInstance);

        // Bind descriptor sets describing shader binding points
        vkCmdBindDescriptorSets(vkCommandbuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_vkPipelineLayout, 0, 1, &uniformsInstance.m_vkDescriptorSet, 0, nullptr);
    }

    const spirv_cross::ShaderResources D3D11ShaderProgram::getSpirvShaderResources(ShaderType type)
    {
        return type == ShaderType::VS ? m_vertexShaderCompiler->get_shader_resources() : m_fragmentShaderCompiler->get_shader_resources();
    }

    const spirv_cross::Compiler* D3D11ShaderProgram::getSpirvShaderCompiler(ShaderType type)
    {
        return type == ShaderType::VS ? m_vertexShaderCompiler : m_fragmentShaderCompiler;
    }
}