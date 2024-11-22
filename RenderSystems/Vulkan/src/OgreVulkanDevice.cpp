/*
-----------------------------------------------------------------------------
This source file is part of OGRE-Next
    (Object-oriented Graphics Rendering Engine)
For the latest info, see http://www.ogre3d.org/

Copyright (c) 2000-present Torus Knot Software Ltd

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
-----------------------------------------------------------------------------
*/

#include "OgreVulkanDevice.h"

#include "OgreVulkanRenderSystem.h"
#include "OgreVulkanWindow.h"
#include "Vao/OgreVulkanVaoManager.h"

#include "OgreException.h"
#include "OgreStringConverter.h"

#include "OgreVulkanUtils.h"

#include "vulkan/vulkan_core.h"

#ifdef OGRE_VULKAN_USE_SWAPPY
#    include "swappy/swappyVk.h"
#endif

#define OGRE_VK_KHR_WIN32_SURFACE_EXTENSION_NAME "VK_KHR_win32_surface"
#define OGRE_VK_KHR_XCB_SURFACE_EXTENSION_NAME "VK_KHR_xcb_surface"
#define OGRE_VK_KHR_ANDROID_SURFACE_EXTENSION_NAME "VK_KHR_android_surface"

#define TODO_findRealPresentQueue

namespace Ogre
{
    FastArray<const char *> VulkanInstance::enabledExtensions;  // sorted
    FastArray<const char *> VulkanInstance::enabledLayers;      // sorted
    bool VulkanInstance::hasValidationLayers = false;

    bool StrCmpLess( const char *a, const char *b ) { return strcmp( a, b ) < 0; }

    void sortAndRelocate( FastArray<const char *> &ar, String &hostStr )
    {
        std::sort( ar.begin(), ar.end(), StrCmpLess );

        size_t sz = 0;
        for( auto p : ar )
            sz += strlen( p ) + 1;

        hostStr.resize( 0 );
        hostStr.reserve( sz );

        for( auto &p : ar )
        {
            sz = hostStr.size();
            hostStr.append( p, strlen( p ) + 1 );  // embedded '\0'
            p = &hostStr[sz];
        }
    };

    //-------------------------------------------------------------------------
    void VulkanInstance::enumerateExtensionsAndLayers( VulkanExternalInstance *externalInstance )
    {
        LogManager::getSingleton().logMessage( externalInstance == nullptr
                                                   ? "Vulkan: Initializing"
                                                   : "Vulkan: Initializing with external VkInstance" );

        // Enumerate supported extensions
        FastArray<VkExtensionProperties> availableExtensions;
        uint32 numExtensions = 0u;
        VkResult result = vkEnumerateInstanceExtensionProperties( 0, &numExtensions, 0 );
        checkVkResult( result, "vkEnumerateInstanceExtensionProperties" );

        availableExtensions.resize( numExtensions );
        result =
            vkEnumerateInstanceExtensionProperties( 0, &numExtensions, availableExtensions.begin() );
        checkVkResult( result, "vkEnumerateInstanceExtensionProperties" );

        for( auto &ext : availableExtensions )
            LogManager::getSingleton().logMessage( "Vulkan: Found instance extension: " +
                                                   String( ext.extensionName ) );

        // Enumerate supported layers
        FastArray<VkLayerProperties> availableLayers;
        uint32 numInstanceLayers = 0u;
        result = vkEnumerateInstanceLayerProperties( &numInstanceLayers, 0 );
        checkVkResult( result, "vkEnumerateInstanceLayerProperties" );

        availableLayers.resize( numInstanceLayers );
        result = vkEnumerateInstanceLayerProperties( &numInstanceLayers, availableLayers.begin() );
        checkVkResult( result, "vkEnumerateInstanceLayerProperties" );

        for( auto &layer : availableLayers )
            LogManager::getSingleton().logMessage( "Vulkan: Found instance layer: " +
                                                   String( layer.layerName ) );

        if( !externalInstance )
        {
            // Enable supported extensions we may want
            enabledExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
            for( auto &ext : availableExtensions )
            {
                const String extensionName = ext.extensionName;
#ifdef OGRE_VULKAN_WINDOW_WIN32
                if( extensionName == OGRE_VK_KHR_WIN32_SURFACE_EXTENSION_NAME )
                    enabledExtensions.push_back( OGRE_VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
#endif
#ifdef OGRE_VULKAN_WINDOW_XCB
                if( extensionName == OGRE_VK_KHR_XCB_SURFACE_EXTENSION_NAME )
                    enabledExtensions.push_back( OGRE_VK_KHR_XCB_SURFACE_EXTENSION_NAME );
#endif
#ifdef OGRE_VULKAN_WINDOW_ANDROID
                if( extensionName == OGRE_VK_KHR_ANDROID_SURFACE_EXTENSION_NAME )
                    enabledExtensions.push_back( OGRE_VK_KHR_ANDROID_SURFACE_EXTENSION_NAME );
#endif
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
                if( extensionName == VK_EXT_DEBUG_REPORT_EXTENSION_NAME )
                    enabledExtensions.push_back( VK_EXT_DEBUG_REPORT_EXTENSION_NAME );
#endif
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_MEDIUM
                if( extensionName == VK_EXT_DEBUG_UTILS_EXTENSION_NAME )
                    enabledExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
#endif
                if( extensionName == VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME )
                    enabledExtensions.push_back(
                        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME );
            }

            // Enable supported layers we may want
            for( auto &layer : availableLayers )
            {
                const String layerName = layer.layerName;
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
                if( layerName == "VK_LAYER_KHRONOS_validation" )
                {
                    enabledLayers.push_back( "VK_LAYER_KHRONOS_validation" );
                    hasValidationLayers = true;
                }
#endif
            }
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
            if( !hasValidationLayers )
            {
                LogManager::getSingleton().logMessage(
                    "WARNING: VK_LAYER_KHRONOS_validation layer not present. "
                    "Extension " VK_EXT_DEBUG_MARKER_EXTENSION_NAME " not found",
                    LML_CRITICAL );
            }
#endif
        }
        else  // externalInstance
        {
            // Filter wrongly-provided extensions
            std::set<String> extensions;
            for( auto &ext : availableExtensions )
                extensions.insert( ext.extensionName );
            auto extFilter = [&extensions]( const VkExtensionProperties &elem ) {
                if( extensions.find( elem.extensionName ) != extensions.end() )
                    return false;
                LogManager::getSingleton().logMessage(
                    "Vulkan: [INFO] External Instance claims extension " + String( elem.extensionName ) +
                    " is present but it's not. This is normal. Ignoring." );
                return true;
            };
            auto &eix = externalInstance->instanceExtensions;
            eix.resize( std::remove_if( eix.begin(), eix.end(), extFilter ) - eix.begin() );
            availableExtensions = eix;
            for( auto &ext : eix )
                enabledExtensions.push_back( ext.extensionName );

            // Filter wrongly-provided layers
            std::set<String> layers;
            for( auto &layer : availableLayers )
                layers.insert( layer.layerName );
            auto layerFilter = [&layers]( const VkLayerProperties &elem ) {
                if( layers.find( elem.layerName ) != layers.end() )
                    return false;
                LogManager::getSingleton().logMessage(
                    "Vulkan: [INFO] External Instance claims layer " + String( elem.layerName ) +
                    " is present but it's not. This is normal. Ignoring." );
                return true;
            };
            auto &eil = externalInstance->instanceLayers;
            eil.resize( std::remove_if( eil.begin(), eil.end(), layerFilter ) - eil.begin() );
            availableLayers = eil;
            for( auto &layer : eil )
                enabledLayers.push_back( layer.layerName );

#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH
            if( layers.find( "VK_LAYER_KHRONOS_validation" ) != layers.end() )
                hasValidationLayers = true;
#endif
        }

        // sort enabled extensions and layers and relocate to heap memory
        static String enabledExtensionsHost, enabledLayersHost;
        sortAndRelocate( enabledExtensions, enabledExtensionsHost );
        sortAndRelocate( enabledLayers, enabledLayersHost );
    }
    //-------------------------------------------------------------------------
    bool VulkanInstance::hasExtension( const char *extension )
    {
        auto itor = std::lower_bound( enabledExtensions.begin(), enabledExtensions.end(), extension,
                                      StrCmpLess );
        return itor != enabledExtensions.end() && 0 == strcmp( *itor, extension );
    }
    //-------------------------------------------------------------------------
    VulkanInstance::VulkanInstance( const String &appName, VulkanExternalInstance *externalInstance,
                                    PFN_vkDebugReportCallbackEXT debugCallback,
                                    RenderSystem *renderSystem ) :
        mVkInstance( externalInstance ? externalInstance->instance : nullptr ),
        mVkInstanceIsExternal( externalInstance && externalInstance->instance ),
        CreateDebugReportCallback( 0 ),
        DestroyDebugReportCallback( 0 ),
        mDebugReportCallback( 0 ),
        CmdBeginDebugUtilsLabelEXT( 0 ),
        CmdEndDebugUtilsLabelEXT( 0 )
    {
        // Log enabled extensions and layers here, so it would be repeated each VkInstance recreation
        String prefix = mVkInstanceIsExternal ? "Vulkan: Externally requested instance extension: "
                                              : "Vulkan: Requesting instance extension: ";
        for( auto &ext : enabledExtensions )
            LogManager::getSingleton().logMessage( prefix + ext );

        prefix = mVkInstanceIsExternal ? "Vulkan: Externally requested instance layer: "
                                       : "Vulkan: Requesting instance layer: ";
        for( auto &layer : enabledLayers )
            LogManager::getSingleton().logMessage( prefix + layer );

        // Create VkInstance if not externally provided
        if( !mVkInstanceIsExternal )
        {
            VkApplicationInfo appInfo;
            makeVkStruct( appInfo, VK_STRUCTURE_TYPE_APPLICATION_INFO );
            if( !appName.empty() )
                appInfo.pApplicationName = appName.c_str();
            appInfo.pEngineName = "Ogre3D Vulkan Engine";
            appInfo.engineVersion = OGRE_VERSION;
            appInfo.apiVersion = VK_MAKE_VERSION( 1, 0, 2 );

            VkInstanceCreateInfo createInfo;
            makeVkStruct( createInfo, VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO );
            createInfo.pApplicationInfo = &appInfo;
            createInfo.enabledLayerCount = static_cast<uint32>( enabledLayers.size() );
            createInfo.ppEnabledLayerNames = enabledLayers.begin();
            createInfo.enabledExtensionCount = static_cast<uint32>( enabledExtensions.size() );
            createInfo.ppEnabledExtensionNames = enabledExtensions.begin();

            // Workaround: skip following code on Android as it causes crash in vkCreateInstance() on
            // Android Emulator 35.1.4, macOS 14.4.1, M1 Pro, despite declared support for rev.10
            // VK_EXT_debug_report
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH && !defined OGRE_VULKAN_WINDOW_ANDROID
            // This is info for a temp callback to use during CreateInstance.
            // After the instance is created, we use the instance-based
            // function to register the final callback.
            VkDebugReportCallbackCreateInfoEXT debugCb;
            makeVkStruct( debugCb, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT );
            debugCb.pfnCallback = debugCallback;
            debugCb.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT;
            debugCb.pUserData = renderSystem;
            createInfo.pNext = &debugCb;
#endif

            VkResult result = vkCreateInstance( &createInfo, 0, &mVkInstance );
            checkVkResult( result, "vkCreateInstance" );
        }

#if OGRE_DEBUG_MODE >= OGRE_DEBUG_MEDIUM
        initDebugFeatures( debugCallback, renderSystem, renderSystem->getRenderDocApi() );
#endif
        initPhysicalDeviceList();
    }
    //-------------------------------------------------------------------------
    VulkanInstance::~VulkanInstance()
    {
        if( mDebugReportCallback )
        {
            DestroyDebugReportCallback( mVkInstance, mDebugReportCallback, 0 );
            mDebugReportCallback = 0;
        }

        if( mVkInstance && !mVkInstanceIsExternal )
        {
            vkDestroyInstance( mVkInstance, 0 );
            mVkInstance = 0;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanInstance::initDebugFeatures( PFN_vkDebugReportCallbackEXT callback, void *userdata,
                                            bool hasRenderDocApi )
    {
#if OGRE_DEBUG_MODE >= OGRE_DEBUG_HIGH  // VK_EXT_debug_report, instance debug callback
        if( !mDebugReportCallback )
        {
            CreateDebugReportCallback = (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(
                mVkInstance, "vkCreateDebugReportCallbackEXT" );
            DestroyDebugReportCallback = (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(
                mVkInstance, "vkDestroyDebugReportCallbackEXT" );
            if( !CreateDebugReportCallback )
            {
                LogManager::getSingleton().logMessage(
                    "Vulkan: GetProcAddr: Unable to find vkCreateDebugReportCallbackEXT. "
                    "Debug reporting won't be available" );
                return;
            }
            if( !DestroyDebugReportCallback )
            {
                LogManager::getSingleton().logMessage(
                    "Vulkan: GetProcAddr: Unable to find vkDestroyDebugReportCallbackEXT. "
                    "Debug reporting won't be available" );
                return;
            }

            VkDebugReportCallbackCreateInfoEXT dbgCreateInfo;
            makeVkStruct( dbgCreateInfo, VK_STRUCTURE_TYPE_DEBUG_REPORT_CREATE_INFO_EXT );
            dbgCreateInfo.pfnCallback = callback;
            dbgCreateInfo.flags = VK_DEBUG_REPORT_ERROR_BIT_EXT | VK_DEBUG_REPORT_WARNING_BIT_EXT |
                                  VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT;
            dbgCreateInfo.pUserData = userdata;
            VkResult result =
                CreateDebugReportCallback( mVkInstance, &dbgCreateInfo, 0, &mDebugReportCallback );
            switch( result )
            {
            case VK_SUCCESS:
                break;
            case VK_ERROR_OUT_OF_HOST_MEMORY:
                OGRE_VK_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, result,
                                "CreateDebugReportCallback: out of host memory",
                                "VulkanInstance::addInstanceDebugCallback" );
            default:
                OGRE_VK_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, result,
                                "vkCreateDebugReportCallbackEXT",
                                "VulkanInstance::addInstanceDebugCallback" );
            }
        }
#endif

#if OGRE_DEBUG_MODE >= OGRE_DEBUG_MEDIUM  // VK_EXT_debug_utils, command buffer labels
        if( !CmdBeginDebugUtilsLabelEXT && !CmdEndDebugUtilsLabelEXT )
        {
            bool bAllow_VK_EXT_debug_utils = false;
            if( hasRenderDocApi )
            {
                // RenderDoc fixes VK_EXT_debug_utils even in older SDKs
                bAllow_VK_EXT_debug_utils = true;
            }
            else
            {
                // vkEnumerateInstanceVersion is available since Vulkan 1.1
                PFN_vkEnumerateInstanceVersion enumerateInstanceVersion =
                    (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(
                        0, "vkEnumerateInstanceVersion" );
                if( enumerateInstanceVersion )
                {
                    uint32_t apiVersion;
                    VkResult result = enumerateInstanceVersion( &apiVersion );
                    if( result == VK_SUCCESS && apiVersion >= VK_MAKE_VERSION( 1, 1, 114 ) )
                    {
                        // Loader version < 1.1.114 is blacklisted as it will just crash.
                        // See https://github.com/KhronosGroup/Vulkan-ValidationLayers/issues/258
                        bAllow_VK_EXT_debug_utils =
                            VulkanInstance::hasExtension( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
                    }
                }
            }

            if( bAllow_VK_EXT_debug_utils )
            {
                // Use VK_EXT_debug_utils.
                CmdBeginDebugUtilsLabelEXT = (PFN_vkCmdBeginDebugUtilsLabelEXT)vkGetInstanceProcAddr(
                    mVkInstance, "vkCmdBeginDebugUtilsLabelEXT" );
                CmdEndDebugUtilsLabelEXT = (PFN_vkCmdEndDebugUtilsLabelEXT)vkGetInstanceProcAddr(
                    mVkInstance, "vkCmdEndDebugUtilsLabelEXT" );
            }
        }
#endif
    }
    //-------------------------------------------------------------------------
    void VulkanInstance::initPhysicalDeviceList()
    {
        LogManager::getSingleton().logMessage( "Vulkan: Device detection starts" );

        // enumerate physical devices - list never changes for the same VkInstance
        FastArray<VkPhysicalDevice> devices;
        uint32 numDevices = 0u;
        VkResult result = vkEnumeratePhysicalDevices( mVkInstance, &numDevices, NULL );
        checkVkResult( result, "vkEnumeratePhysicalDevices" );

        devices.resize( numDevices );
        result = vkEnumeratePhysicalDevices( mVkInstance, &numDevices, devices.begin() );
        checkVkResult( result, "vkEnumeratePhysicalDevices" );

        if( numDevices == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "No Vulkan devices found.",
                         "VulkanRenderSystem::getVkPhysicalDevices" );
        }

        // assign unique names, allowing reordering/inserting/removing
        map<String, unsigned>::type sameNameCounter;
        mVulkanPhysicalDevices.clear();
        mVulkanPhysicalDevices.reserve( devices.size() );
        for( auto device : devices )
        {
            VkPhysicalDeviceProperties deviceProps;
            vkGetPhysicalDeviceProperties( device, &deviceProps );

            String name( deviceProps.deviceName );
            unsigned sameNameIndex = sameNameCounter[name]++;  // inserted entry is zero-initialized
            if( sameNameIndex != 0 )
                name += " (" + Ogre::StringConverter::toString( sameNameIndex + 1 ) + ")";

            LogManager::getSingleton().logMessage( "Vulkan: \"" + name + "\"" );
            mVulkanPhysicalDevices.push_back( { device, name } );
        }

        LogManager::getSingleton().logMessage( "Vulkan: Device detection ends" );
    }
    //-------------------------------------------------------------------------
    const VulkanPhysicalDevice *VulkanInstance::findByName( const String &name ) const
    {
        // return requested device
        if( !name.empty() )
            for( auto &elem : mVulkanPhysicalDevices )
                if( elem.title == name )
                    return &elem;

        // return default device
        if( !mVulkanPhysicalDevices.empty() )
            return &mVulkanPhysicalDevices[0];

        return nullptr;
    }
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    VulkanDevice::VulkanDevice( VkInstance instance, const VulkanPhysicalDevice &physicalDevice,
                                VulkanRenderSystem *renderSystem ) :
        mInstance( instance ),
        mPhysicalDevice( 0 ),
        mDevice( 0 ),
        mPipelineCache( 0 ),
        mPresentQueue( 0 ),
        mVaoManager( 0 ),
        mRenderSystem( renderSystem ),
        mSupportedStages( 0xFFFFFFFF ),
        mIsExternal( false )
    {
        memset( &mDeviceMemoryProperties, 0, sizeof( mDeviceMemoryProperties ) );
        setPhysicalDevice( physicalDevice );
    }
    //-------------------------------------------------------------------------
    VulkanDevice::VulkanDevice( VkInstance instance, const VulkanExternalDevice &externalDevice,
                                VulkanRenderSystem *renderSystem ) :
        mInstance( instance ),
        mPhysicalDevice( externalDevice.physicalDevice ),
        mDevice( externalDevice.device ),
        mPipelineCache( 0 ),
        mPresentQueue( 0 ),
        mVaoManager( 0 ),
        mRenderSystem( renderSystem ),
        mSupportedStages( 0xFFFFFFFF ),
        mIsExternal( true )
    {
        LogManager::getSingleton().logMessage(
            "Vulkan: Creating Vulkan Device from External VkVulkan handle" );

        memset( &mDeviceMemoryProperties, 0, sizeof( mDeviceMemoryProperties ) );

        vkGetPhysicalDeviceMemoryProperties( mPhysicalDevice, &mDeviceMemoryProperties );

        // mDeviceExtraFeatures gets initialized later, once we analyzed all the extensions
        memset( &mDeviceExtraFeatures, 0, sizeof( mDeviceExtraFeatures ) );
        fillDeviceFeatures();

        mSupportedStages = 0xFFFFFFFF;
        if( !mDeviceFeatures.geometryShader )
            mSupportedStages ^= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        if( !mDeviceFeatures.tessellationShader )
        {
            mSupportedStages ^= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                                VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        }

        {
            uint32 numQueues;
            vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, NULL );
            if( numQueues == 0u )
            {
                OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Vulkan device is reporting 0 queues!",
                             "VulkanDevice::createDevice" );
            }
            mQueueProps.resize( numQueues );
            vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, &mQueueProps[0] );
        }

        mPresentQueue = externalDevice.presentQueue;
        mGraphicsQueue.setExternalQueue( this, VulkanQueue::Graphics, externalDevice.graphicsQueue );
        {
            // Filter wrongly-provided extensions
            uint32 numExtensions = 0;
            vkEnumerateDeviceExtensionProperties( mPhysicalDevice, 0, &numExtensions, 0 );

            FastArray<VkExtensionProperties> availableExtensions;
            availableExtensions.resize( numExtensions );
            vkEnumerateDeviceExtensionProperties( mPhysicalDevice, 0, &numExtensions,
                                                  availableExtensions.begin() );
            std::set<String> extensions;
            for( size_t i = 0u; i < numExtensions; ++i )
            {
                const String extensionName = availableExtensions[i].extensionName;
                LogManager::getSingleton().logMessage( "Vulkan: Found device extension: " +
                                                       extensionName );
                extensions.insert( extensionName );
            }

            FastArray<VkExtensionProperties> deviceExtensionsCopy = externalDevice.deviceExtensions;
            FastArray<VkExtensionProperties>::iterator itor = deviceExtensionsCopy.begin();
            FastArray<VkExtensionProperties>::iterator endt = deviceExtensionsCopy.end();

            while( itor != endt )
            {
                if( extensions.find( itor->extensionName ) == extensions.end() )
                {
                    LogManager::getSingleton().logMessage(
                        "Vulkan: [INFO] External Device claims extension " +
                        String( itor->extensionName ) +
                        " is present but it's not. This is normal. Ignoring." );
                    itor = efficientVectorRemove( deviceExtensionsCopy, itor );
                    endt = deviceExtensionsCopy.end();
                }
                else
                {
                    ++itor;
                }
            }

            mDeviceExtensions.reserve( deviceExtensionsCopy.size() );
            itor = deviceExtensionsCopy.begin();
            while( itor != endt )
            {
                LogManager::getSingleton().logMessage(
                    "Vulkan: Externally requested Device Extension: " + String( itor->extensionName ) );
                mDeviceExtensions.push_back( itor->extensionName );
                ++itor;
            }

            std::sort( mDeviceExtensions.begin(), mDeviceExtensions.end() );
        }

        // Initialize mDeviceExtraFeatures
        if( VulkanInstance::hasExtension( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) )
        {
            VkPhysicalDeviceFeatures2 deviceFeatures2;
            makeVkStruct( deviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 );
            VkPhysicalDevice16BitStorageFeatures _16BitStorageFeatures;
            makeVkStruct( _16BitStorageFeatures,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES );
            VkPhysicalDeviceShaderFloat16Int8Features shaderFloat16Int8Features;
            makeVkStruct( shaderFloat16Int8Features,
                          VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES );

            PFN_vkGetPhysicalDeviceFeatures2KHR GetPhysicalDeviceFeatures2KHR =
                (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(
                    mInstance, "vkGetPhysicalDeviceFeatures2KHR" );

            void **lastNext = &deviceFeatures2.pNext;

            if( this->hasDeviceExtension( VK_KHR_16BIT_STORAGE_EXTENSION_NAME ) )
            {
                *lastNext = &_16BitStorageFeatures;
                lastNext = &_16BitStorageFeatures.pNext;
            }
            if( this->hasDeviceExtension( VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME ) )
            {
                *lastNext = &shaderFloat16Int8Features;
                lastNext = &shaderFloat16Int8Features.pNext;
            }

            GetPhysicalDeviceFeatures2KHR( mPhysicalDevice, &deviceFeatures2 );

            mDeviceExtraFeatures.storageInputOutput16 = _16BitStorageFeatures.storageInputOutput16;
            mDeviceExtraFeatures.shaderFloat16 = shaderFloat16Int8Features.shaderFloat16;
            mDeviceExtraFeatures.shaderInt8 = shaderFloat16Int8Features.shaderInt8;
        }

        initUtils( mDevice );
    }
    //-------------------------------------------------------------------------
    VulkanDevice::~VulkanDevice()
    {
        if( mDevice )
        {
            vkDeviceWaitIdle( mDevice );

            mGraphicsQueue.destroy();
            destroyQueues( mComputeQueues );
            destroyQueues( mTransferQueues );

            if( mPipelineCache )
            {
                vkDestroyPipelineCache( mDevice, mPipelineCache, nullptr );
                mPipelineCache = 0;
            }

            // Must be done externally (this' destructor has yet to free more Vulkan stuff)
            // vkDestroyDevice( mDevice, 0 );
            mDevice = 0;
            mPhysicalDevice = 0;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::fillDeviceFeatures()
    {
#define VK_DEVICEFEATURE_ENABLE_IF( x ) \
    if( features.x ) \
    mDeviceFeatures.x = features.x

        VkPhysicalDeviceFeatures features;
        vkGetPhysicalDeviceFeatures( mPhysicalDevice, &features );

        // Don't opt in to features we don't want / need.
        memset( &mDeviceFeatures, 0, sizeof( mDeviceFeatures ) );
        // VK_DEVICEFEATURE_ENABLE_IF( robustBufferAccess );
        VK_DEVICEFEATURE_ENABLE_IF( fullDrawIndexUint32 );
        VK_DEVICEFEATURE_ENABLE_IF( imageCubeArray );
        VK_DEVICEFEATURE_ENABLE_IF( independentBlend );
        VK_DEVICEFEATURE_ENABLE_IF( geometryShader );
        VK_DEVICEFEATURE_ENABLE_IF( tessellationShader );
        VK_DEVICEFEATURE_ENABLE_IF( sampleRateShading );
        VK_DEVICEFEATURE_ENABLE_IF( dualSrcBlend );
        // VK_DEVICEFEATURE_ENABLE_IF( logicOp );
        VK_DEVICEFEATURE_ENABLE_IF( multiDrawIndirect );
        VK_DEVICEFEATURE_ENABLE_IF( drawIndirectFirstInstance );
        VK_DEVICEFEATURE_ENABLE_IF( depthClamp );
        VK_DEVICEFEATURE_ENABLE_IF( depthBiasClamp );
        VK_DEVICEFEATURE_ENABLE_IF( fillModeNonSolid );
        VK_DEVICEFEATURE_ENABLE_IF( depthBounds );
        // VK_DEVICEFEATURE_ENABLE_IF( wideLines );
        // VK_DEVICEFEATURE_ENABLE_IF( largePoints );
        VK_DEVICEFEATURE_ENABLE_IF( alphaToOne );
        VK_DEVICEFEATURE_ENABLE_IF( multiViewport );
        VK_DEVICEFEATURE_ENABLE_IF( samplerAnisotropy );
        VK_DEVICEFEATURE_ENABLE_IF( textureCompressionETC2 );
        VK_DEVICEFEATURE_ENABLE_IF( textureCompressionASTC_LDR );
        VK_DEVICEFEATURE_ENABLE_IF( textureCompressionBC );
        // VK_DEVICEFEATURE_ENABLE_IF( occlusionQueryPrecise );
        // VK_DEVICEFEATURE_ENABLE_IF( pipelineStatisticsQuery );
        VK_DEVICEFEATURE_ENABLE_IF( vertexPipelineStoresAndAtomics );
        VK_DEVICEFEATURE_ENABLE_IF( fragmentStoresAndAtomics );
        VK_DEVICEFEATURE_ENABLE_IF( shaderTessellationAndGeometryPointSize );
        VK_DEVICEFEATURE_ENABLE_IF( shaderImageGatherExtended );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageImageExtendedFormats );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageImageMultisample );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageImageReadWithoutFormat );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageImageWriteWithoutFormat );
        VK_DEVICEFEATURE_ENABLE_IF( shaderUniformBufferArrayDynamicIndexing );
        VK_DEVICEFEATURE_ENABLE_IF( shaderSampledImageArrayDynamicIndexing );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageBufferArrayDynamicIndexing );
        VK_DEVICEFEATURE_ENABLE_IF( shaderStorageImageArrayDynamicIndexing );
        VK_DEVICEFEATURE_ENABLE_IF( shaderClipDistance );
        VK_DEVICEFEATURE_ENABLE_IF( shaderCullDistance );
        // VK_DEVICEFEATURE_ENABLE_IF( shaderFloat64 );
        // VK_DEVICEFEATURE_ENABLE_IF( shaderInt64 );
        VK_DEVICEFEATURE_ENABLE_IF( shaderInt16 );
        // VK_DEVICEFEATURE_ENABLE_IF( shaderResourceResidency );
        VK_DEVICEFEATURE_ENABLE_IF( shaderResourceMinLod );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseBinding );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidencyBuffer );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidencyImage2D );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidencyImage3D );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidency2Samples );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidency4Samples );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidency8Samples );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidency16Samples );
        // VK_DEVICEFEATURE_ENABLE_IF( sparseResidencyAliased );
        VK_DEVICEFEATURE_ENABLE_IF( variableMultisampleRate );
        // VK_DEVICEFEATURE_ENABLE_IF( inheritedQueries );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::destroyQueues( FastArray<VulkanQueue> &queueArray )
    {
        FastArray<VulkanQueue>::iterator itor = queueArray.begin();
        FastArray<VulkanQueue>::iterator endt = queueArray.end();

        while( itor != endt )
        {
            itor->destroy();
            ++itor;
        }
        queueArray.clear();
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::setPhysicalDevice( const VulkanPhysicalDevice &physicalDevice )
    {
        if( !physicalDevice.title.empty() )
            LogManager::getSingleton().logMessage( "Vulkan: Selected \"" + physicalDevice.title +
                                                   "\" physical device" );

        mPhysicalDevice = physicalDevice.physicalDevice;

        vkGetPhysicalDeviceMemoryProperties( mPhysicalDevice, &mDeviceMemoryProperties );

        // mDeviceExtraFeatures gets initialized later, once we analyzed all the extensions
        memset( &mDeviceExtraFeatures, 0, sizeof( mDeviceExtraFeatures ) );
        fillDeviceFeatures();

        mSupportedStages = 0xFFFFFFFF;
        if( !mDeviceFeatures.geometryShader )
            mSupportedStages ^= VK_PIPELINE_STAGE_GEOMETRY_SHADER_BIT;
        if( !mDeviceFeatures.tessellationShader )
        {
            mSupportedStages ^= VK_PIPELINE_STAGE_TESSELLATION_CONTROL_SHADER_BIT |
                                VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findGraphicsQueue( FastArray<uint32> &inOutUsedQueueCount )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mGraphicsQueue.setQueueData( this, VulkanQueue::Graphics, static_cast<uint32>( i ),
                                             inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
                return;
            }
        }

        OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR,
                     "GPU does not expose Graphics queue. Cannot be used for rendering",
                     "VulkanQueue::findGraphicsQueue" );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findComputeQueue( FastArray<uint32> &inOutUsedQueueCount, uint32 maxNumQueues )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues && mComputeQueues.size() < maxNumQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_COMPUTE_BIT &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mComputeQueues.push_back( VulkanQueue() );
                mComputeQueues.back().setQueueData( this, VulkanQueue::Compute, static_cast<uint32>( i ),
                                                    inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::findTransferQueue( FastArray<uint32> &inOutUsedQueueCount, uint32 maxNumQueues )
    {
        const size_t numQueues = mQueueProps.size();
        for( size_t i = 0u; i < numQueues && mTransferQueues.size() < maxNumQueues; ++i )
        {
            if( mQueueProps[i].queueFlags & VK_QUEUE_TRANSFER_BIT &&
                !( mQueueProps[i].queueFlags & ( VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT ) ) &&
                inOutUsedQueueCount[i] < mQueueProps[i].queueCount )
            {
                mTransferQueues.push_back( VulkanQueue() );
                mTransferQueues.back().setQueueData( this, VulkanQueue::Transfer,
                                                     static_cast<uint32>( i ), inOutUsedQueueCount[i] );
                ++inOutUsedQueueCount[i];
            }
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::fillQueueCreationInfo( uint32 maxComputeQueues, uint32 maxTransferQueues,
                                              FastArray<VkDeviceQueueCreateInfo> &outQueueCiArray )
    {
        const size_t numQueueFamilies = mQueueProps.size();

        FastArray<uint32> usedQueueCount;
        usedQueueCount.resize( numQueueFamilies, 0u );

        findGraphicsQueue( usedQueueCount );
        findComputeQueue( usedQueueCount, maxComputeQueues );
        findTransferQueue( usedQueueCount, maxTransferQueues );

        VkDeviceQueueCreateInfo queueCi;
        makeVkStruct( queueCi, VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO );

        for( size_t i = 0u; i < numQueueFamilies; ++i )
        {
            queueCi.queueFamilyIndex = static_cast<uint32>( i );
            queueCi.queueCount = usedQueueCount[i];
            if( queueCi.queueCount > 0u )
                outQueueCiArray.push_back( queueCi );
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::createDevice( FastArray<const char *> &extensions, uint32 maxComputeQueues,
                                     uint32 maxTransferQueues )
    {
        uint32 numQueues;
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, NULL );
        if( numQueues == 0u )
        {
            OGRE_EXCEPT( Exception::ERR_RENDERINGAPI_ERROR, "Vulkan device is reporting 0 queues!",
                         "VulkanDevice::createDevice" );
        }
        mQueueProps.resize( numQueues );
        vkGetPhysicalDeviceQueueFamilyProperties( mPhysicalDevice, &numQueues, &mQueueProps[0] );

        // Setup queue creation
        FastArray<VkDeviceQueueCreateInfo> queueCreateInfo;
        fillQueueCreationInfo( maxComputeQueues, maxTransferQueues, queueCreateInfo );

        FastArray<FastArray<float> > queuePriorities;
        queuePriorities.resize( queueCreateInfo.size() );

        for( size_t i = 0u; i < queueCreateInfo.size(); ++i )
        {
            queuePriorities[i].resize( queueCreateInfo[i].queueCount, 1.0f );
            queueCreateInfo[i].pQueuePriorities = queuePriorities[i].begin();
        }

        extensions.push_back( VK_KHR_SWAPCHAIN_EXTENSION_NAME );

        VkDeviceCreateInfo createInfo;
        makeVkStruct( createInfo, VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO );

        createInfo.enabledExtensionCount = static_cast<uint32>( extensions.size() );
        createInfo.ppEnabledExtensionNames = extensions.begin();

        createInfo.queueCreateInfoCount = static_cast<uint32>( queueCreateInfo.size() );
        createInfo.pQueueCreateInfos = &queueCreateInfo[0];

        if( VulkanInstance::hasExtension( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) )
            createInfo.pEnabledFeatures = nullptr;
        else
            createInfo.pEnabledFeatures = &mDeviceFeatures;

        {
            mDeviceExtensions.clear();
            mDeviceExtensions.reserve( extensions.size() );

            FastArray<const char *>::const_iterator itor = extensions.begin();
            FastArray<const char *>::const_iterator endt = extensions.end();

            while( itor != endt )
            {
                LogManager::getSingleton().logMessage( "Vulkan: Requesting Extension: " +
                                                       String( *itor ) );
                mDeviceExtensions.push_back( *itor );
                ++itor;
            }

            std::sort( mDeviceExtensions.begin(), mDeviceExtensions.end() );
        }

        // Declared at this scope because they must be alive for createInfo.pNext
        VkPhysicalDeviceFeatures2 deviceFeatures2;
        makeVkStruct( deviceFeatures2, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 );
        VkPhysicalDevice16BitStorageFeatures _16BitStorageFeatures;
        makeVkStruct( _16BitStorageFeatures, VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES );
        VkPhysicalDeviceShaderFloat16Int8Features shaderFloat16Int8Features;
        makeVkStruct( shaderFloat16Int8Features,
                      VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES );

        // Initialize mDeviceExtraFeatures
        if( VulkanInstance::hasExtension( VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME ) )
        {
            PFN_vkGetPhysicalDeviceFeatures2KHR GetPhysicalDeviceFeatures2KHR =
                (PFN_vkGetPhysicalDeviceFeatures2KHR)vkGetInstanceProcAddr(
                    mInstance, "vkGetPhysicalDeviceFeatures2KHR" );

            void **lastNext = &deviceFeatures2.pNext;

            if( this->hasDeviceExtension( VK_KHR_16BIT_STORAGE_EXTENSION_NAME ) )
            {
                *lastNext = &_16BitStorageFeatures;
                lastNext = &_16BitStorageFeatures.pNext;
            }
            if( this->hasDeviceExtension( VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME ) )
            {
                *lastNext = &shaderFloat16Int8Features;
                lastNext = &shaderFloat16Int8Features.pNext;
            }

            // Send the same chain to vkCreateDevice
            createInfo.pNext = &deviceFeatures2;

            GetPhysicalDeviceFeatures2KHR( mPhysicalDevice, &deviceFeatures2 );
            deviceFeatures2.features = mDeviceFeatures;

            mDeviceExtraFeatures.storageInputOutput16 = _16BitStorageFeatures.storageInputOutput16;
            mDeviceExtraFeatures.shaderFloat16 = shaderFloat16Int8Features.shaderFloat16;
            mDeviceExtraFeatures.shaderInt8 = shaderFloat16Int8Features.shaderInt8;
        }

        VkResult result = vkCreateDevice( mPhysicalDevice, &createInfo, NULL, &mDevice );
        checkVkResult( result, "vkCreateDevice" );

        VkPipelineCacheCreateInfo pipelineCacheCreateInfo;
        makeVkStruct( pipelineCacheCreateInfo, VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO );

        result = vkCreatePipelineCache( mDevice, &pipelineCacheCreateInfo, nullptr, &mPipelineCache );
        checkVkResult( result, "vkCreatePipelineCache" );

        initUtils( mDevice );
    }
    //-------------------------------------------------------------------------
    bool VulkanDevice::hasDeviceExtension( const IdString extension ) const
    {
        FastArray<IdString>::const_iterator itor =
            std::lower_bound( mDeviceExtensions.begin(), mDeviceExtensions.end(), extension );
        return itor != mDeviceExtensions.end() && *itor == extension;
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::initQueues()
    {
        if( !mIsExternal )
        {
            VkQueue queue = 0;
            vkGetDeviceQueue( mDevice, mGraphicsQueue.mFamilyIdx, mGraphicsQueue.mQueueIdx, &queue );
            mGraphicsQueue.init( mDevice, queue, mRenderSystem );

            TODO_findRealPresentQueue;
            mPresentQueue = mGraphicsQueue.mQueue;
        }
        else
        {
            mGraphicsQueue.init( mDevice, mGraphicsQueue.mQueue, mRenderSystem );
        }

#ifdef OGRE_VULKAN_USE_SWAPPY
        SwappyVk_setQueueFamilyIndex( mDevice, mGraphicsQueue.mQueue, mGraphicsQueue.mFamilyIdx );
#endif
        VkQueue queue = 0;

        FastArray<VulkanQueue>::iterator itor = mComputeQueues.begin();
        FastArray<VulkanQueue>::iterator endt = mComputeQueues.end();

        while( itor != endt )
        {
            vkGetDeviceQueue( mDevice, itor->mFamilyIdx, itor->mQueueIdx, &queue );
            itor->init( mDevice, queue, mRenderSystem );
            ++itor;
        }

        itor = mTransferQueues.begin();
        endt = mTransferQueues.end();

        while( itor != endt )
        {
            vkGetDeviceQueue( mDevice, itor->mFamilyIdx, itor->mQueueIdx, &queue );
            itor->init( mDevice, queue, mRenderSystem );
            ++itor;
        }
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::commitAndNextCommandBuffer( SubmissionType::SubmissionType submissionType )
    {
        mGraphicsQueue.endAllEncoders();
        mGraphicsQueue.commitAndNextCommandBuffer( submissionType );
    }
    //-------------------------------------------------------------------------
    void VulkanDevice::stall()
    {
        // We must call flushBoundGpuProgramParameters() now because commitAndNextCommandBuffer() will
        // call flushBoundGpuProgramParameters( SubmissionType::FlushOnly ).
        //
        // Unfortunately that's not enough because that assumes VaoManager::mFrameCount
        // won't change (which normally wouldn't w/ FlushOnly). However our caller is
        // about to do mFrameCount += mDynamicBufferMultiplier.
        //
        // The solution is to tell flushBoundGpuProgramParameters() we're changing frame idx.
        // Calling it again becomes a no-op since mFirstUnflushedAutoParamsBuffer becomes 0.
        //
        // We also *want* mCurrentAutoParamsBufferPtr to become nullptr since it can be reused from
        // scratch. Because we're stalling, it is safe to do so.
        mRenderSystem->flushBoundGpuProgramParameters( SubmissionType::NewFrameIdx );

        // We must flush the cmd buffer and our bindings because we take the
        // moment to delete all delayed buffers and API handles after a stall.
        //
        // We can't have potentially dangling API handles in a cmd buffer.
        // We must submit our current pending work so far and wait until that's done.
        commitAndNextCommandBuffer( SubmissionType::FlushOnly );
        mRenderSystem->resetAllBindings();

        vkDeviceWaitIdle( mDevice );

        mRenderSystem->_notifyDeviceStalled();
    }
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    //-------------------------------------------------------------------------
    VulkanDevice::SelectedQueue::SelectedQueue() :
        usage( VulkanQueue::NumQueueFamilies ),
        familyIdx( std::numeric_limits<uint32>::max() ),
        queueIdx( 0 )
    {
    }
}  // namespace Ogre
