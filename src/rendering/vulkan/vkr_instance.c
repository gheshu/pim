#include "rendering/vulkan/vkr_instance.h"
#include "rendering/vulkan/vkr_debug.h"
#include "rendering/vulkan/vkr_mem.h"
#include "allocator/allocator.h"
#include "common/console.h"
#include "common/stringutil.h"
#include <string.h>
#include <GLFW/glfw3.h>

bool vkrInstance_Init(vkr_t* vkr)
{
    ASSERT(vkr);

    VkCheck(volkInitialize());

    vkrListInstLayers();
    vkrListInstExtensions();

    vkr->inst = vkrCreateInstance(vkrGetInstExtensions(), vkrGetLayers());
    ASSERT(vkr->inst);
    if (!vkr->inst)
    {
        return false;
    }

    volkLoadInstance(vkr->inst);

    vkr->messenger = vkrCreateDebugMessenger();

    return true;
}

void vkrInstance_Shutdown(vkr_t* vkr)
{
    if (vkr)
    {
        vkrDestroyDebugMessenger(vkr->messenger);
        vkr->messenger = NULL;
        if (vkr->inst)
        {
            vkDestroyInstance(vkr->inst, NULL);
            vkr->inst = NULL;
        }
    }
}

// ----------------------------------------------------------------------------

strlist_t vkrGetLayers(void)
{
    strlist_t list;
    strlist_new(&list, EAlloc_Temp);

    u32 count = 0;
    const VkLayerProperties* props = vkrEnumInstLayers(&count);

#if VKR_KHRONOS_LAYER_ON
    if (!vkrTryAddLayer(&list, props, count, VKR_KHRONOS_LAYER_NAME))
    {
        con_logf(LogSev_Warning, "vkr", "Failed to load layer '%s'", VKR_KHRONOS_LAYER_NAME);
    }
#endif // VKR_KHRONOS_LAYER_ON

#if VKR_ASSIST_LAYER_ON
    if (!vkrTryAddLayer(&list, props, count, VKR_ASSIST_LAYER_NAME))
    {
        con_logf(LogSev_Warning, "vkr", "Failed to load layer '%s'", VKR_ASSIST_LAYER_NAME);
    }
#endif // VKR_ASSIST_LAYER_ON

    return list;
}

static const char* const kDesiredInstExtensions[] =
{
    VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
    VK_EXT_SWAPCHAIN_COLOR_SPACE_EXTENSION_NAME,
#if VKR_DEBUG_MESSENGER_ON
    VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
#endif // VKR_DEBUG_MESSENGER_ON
};

strlist_t vkrGetInstExtensions(void)
{
    strlist_t list;
    strlist_new(&list, EAlloc_Temp);

    u32 count = 0;
    const VkExtensionProperties* props = vkrEnumInstExtensions(&count);

    u32 glfwCount = 0;
    const char** glfwList = glfwGetRequiredInstanceExtensions(&glfwCount);
    for (u32 i = 0; i < glfwCount; ++i)
    {
        if (!vkrTryAddExtension(&list, props, count, glfwList[i]))
        {
            con_logf(LogSev_Error, "vkr", "Failed to load required instance extension '%'", glfwList[i]);
            ASSERT(false);
        }
    }

    for (i32 i = 0; i < NELEM(kDesiredInstExtensions); ++i)
    {
        if (!vkrTryAddExtension(&list, props, count, kDesiredInstExtensions[i]))
        {
            con_logf(LogSev_Warning, "vkr", "Failed to load desired instance extension '%'", kDesiredInstExtensions[i]);
        }
    }

    return list;
}

VkLayerProperties* vkrEnumInstLayers(u32* countOut)
{
    ASSERT(countOut);
    u32 count = 0;
    VkLayerProperties* props = NULL;
    VkCheck(vkEnumerateInstanceLayerProperties(&count, NULL));
    TempReserve(props, count);
    VkCheck(vkEnumerateInstanceLayerProperties(&count, props));
    *countOut = count;
    return props;
}

VkExtensionProperties* vkrEnumInstExtensions(u32* countOut)
{
    ASSERT(countOut);
    u32 count = 0;
    VkExtensionProperties* props = NULL;
    VkCheck(vkEnumerateInstanceExtensionProperties(NULL, &count, NULL));
    TempReserve(props, count);
    VkCheck(vkEnumerateInstanceExtensionProperties(NULL, &count, props));
    *countOut = count;
    return props;
}

void vkrListInstLayers(void)
{
    u32 count = 0;
    VkLayerProperties* props = vkrEnumInstLayers(&count);
    con_logf(LogSev_Info, "vkr", "%d available instance layers", count);
    for (u32 i = 0; i < count; ++i)
    {
        con_logf(LogSev_Info, "vkr", props[i].layerName);
    }
}

void vkrListInstExtensions(void)
{
    u32 count = 0;
    VkExtensionProperties* props = vkrEnumInstExtensions(&count);
    con_logf(LogSev_Info, "vkr", "%d available instance extensions", count);
    for (u32 i = 0; i < count; ++i)
    {
        con_logf(LogSev_Info, "vkr", props[i].extensionName);
    }
}

i32 vkrFindExtension(
    const VkExtensionProperties* props,
    u32 count,
    const char* name)
{
    ASSERT(props || !count);
    ASSERT(name);
    for (u32 i = 0; i < count; ++i)
    {
        if (StrCmp(ARGS(props[i].extensionName), name) == 0)
        {
            return (i32)i;
        }
    }
    return -1;
}

i32 vkrFindLayer(
    const VkLayerProperties* props,
    u32 count,
    const char* name)
{
    ASSERT(props || !count);
    ASSERT(name);
    for (u32 i = 0; i < count; ++i)
    {
        if (StrCmp(ARGS(props[i].layerName), name) == 0)
        {
            return (i32)i;
        }
    }
    return -1;
}

bool vkrTryAddLayer(
    strlist_t* list,
    const VkLayerProperties* props,
    u32 propCount,
    const char* name)
{
    ASSERT(list);
    if (vkrFindLayer(props, propCount, name) >= 0)
    {
        strlist_add(list, name);
        return true;
    }
    return false;
}

bool vkrTryAddExtension(
    strlist_t* list,
    const VkExtensionProperties* props,
    u32 propCount,
    const char* name)
{
    ASSERT(list);
    if (vkrFindExtension(props, propCount, name) >= 0)
    {
        strlist_add(list, name);
        return true;
    }
    return false;
}

VkInstance vkrCreateInstance(strlist_t extensions, strlist_t layers)
{
    const VkApplicationInfo appInfo =
    {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "pimquake",
        .applicationVersion = 1,
        .pEngineName = "pim",
        .engineVersion = 1,
        .apiVersion = VK_API_VERSION_1_2,
    };

    const VkInstanceCreateInfo instInfo =
    {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .flags = 0x0,
        .pApplicationInfo = &appInfo,
        .enabledLayerCount = layers.count,
        .ppEnabledLayerNames = layers.ptr,
        .enabledExtensionCount = extensions.count,
        .ppEnabledExtensionNames = extensions.ptr,
    };

    VkInstance inst = NULL;
    VkCheck(vkCreateInstance(&instInfo, NULL, &inst));

    strlist_del(&extensions);
    strlist_del(&layers);

    return inst;
}
