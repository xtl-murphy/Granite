#ifdef HAVE_ANDROID_SURFACE
#define VK_USE_PLATFORM_ANDROID_KHR
#endif

#include "wsi.hpp"
#include "vulkan_symbol_wrapper.h"
#include "vulkan_events.hpp"

#if defined(HAVE_GLFW)
#include <GLFW/glfw3.h>
#endif

extern bool mainloop_step(Vulkan::WSI &wsi);

using namespace std;

namespace Vulkan
{

#ifdef ANDROID
static ANativeWindow *native_window;
void WSI::set_global_native_window(ANativeWindow *window)
{
	native_window = window;
}

void WSI::runtime_term_native_window()
{

}

void WSI::runtime_init_native_window(ANativeWindow *window)
{

}
#endif

bool WSI::alive()
{
#if defined(HAVE_GLFW)
	glfwPollEvents();
	return !glfwWindowShouldClose(window);
#elif defined(HAVE_ANDROID_SURFACE)
	return mainloop_step(*this);
#else
	return true;
#endif
}

void WSI::poll_input()
{
#if defined(HAVE_GLFW)
	glfwPollEvents();
#endif
	tracker.dispatch_current_state(timer.get_frame_time());
}

#if defined(HAVE_GLFW)
static void fb_size_cb(GLFWwindow *window, int width, int height)
{
	auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));
	VK_ASSERT(width != 0 && height != 0);
	wsi->update_framebuffer(width, height);
}

static Key glfw_key_to_granite(int key)
{
#define k(glfw, granite) case GLFW_KEY_##glfw: return Key::granite
	switch (key)
	{
	k(A, A);
	k(B, B);
	k(C, C);
	k(D, D);
	k(E, E);
	k(F, F);
	k(G, G);
	k(H, H);
	k(I, I);
	k(J, J);
	k(K, K);
	k(L, L);
	k(M, M);
	k(N, N);
	k(O, O);
	k(P, P);
	k(Q, Q);
	k(R, R);
	k(S, S);
	k(T, T);
	k(U, U);
	k(V, V);
	k(W, W);
	k(X, X);
	k(Y, Y);
	k(Z, Z);
	k(LEFT_CONTROL, LeftCtrl);
	k(LEFT_ALT, LeftAlt);
	k(LEFT_SHIFT, LeftShift);
	k(ENTER, Return);
	k(SPACE, Space);
	k(ESCAPE, Escape);
	default:
		return Key::Unknown;
	}
#undef k
}

static void key_cb(GLFWwindow *window, int key, int, int action, int)
{
	KeyState state;
	switch (action)
	{
	case GLFW_PRESS:
		state = KeyState::Pressed;
		break;

	default:
	case GLFW_RELEASE:
		state = KeyState::Released;
		break;

	case GLFW_REPEAT:
		state = KeyState::Repeat;
		break;
	}

	auto gkey = glfw_key_to_granite(key);
	auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));
	wsi->get_input_tracker().key_event(gkey, state);
}

static void button_cb(GLFWwindow *window, int button, int action, int)
{
	auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));

	MouseButton btn;
	switch (button)
	{
	default:
	case GLFW_MOUSE_BUTTON_LEFT:
		btn = MouseButton::Left;
		break;
	case GLFW_MOUSE_BUTTON_RIGHT:
		btn = MouseButton::Right;
		break;
	case GLFW_MOUSE_BUTTON_MIDDLE:
		btn = MouseButton::Middle;
		break;
	}
	wsi->get_input_tracker().mouse_button_event(btn, action == GLFW_PRESS);
}

static void cursor_cb(GLFWwindow *window, double x, double y)
{
	auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));
	wsi->get_input_tracker().mouse_move_event(x, y);
}

static void enter_cb(GLFWwindow *window, int entered)
{
	auto *wsi = static_cast<WSI *>(glfwGetWindowUserPointer(window));
	if (entered)
	{
		double x, y;
		glfwGetCursorPos(window, &x, &y);
		wsi->get_input_tracker().mouse_enter(x, y);
	}
	else
		wsi->get_input_tracker().mouse_leave();
}
#endif

#if defined(HAVE_KHR_DISPLAY)
static bool vulkan_update_display_mode(unsigned *width, unsigned *height, const VkDisplayModePropertiesKHR *mode,
                                       unsigned desired_width, unsigned desired_height)
{
	unsigned visible_width = mode->parameters.visibleRegion.width;
	unsigned visible_height = mode->parameters.visibleRegion.height;

	if (!desired_width || !desired_height)
	{
		/* Strategy here is to pick something which is largest resolution. */
		unsigned area = visible_width * visible_height;
		if (area > (*width) * (*height))
		{
			*width = visible_width;
			*height = visible_height;
			return true;
		}
		else
			return false;
	}
	else
	{
		/* For particular resolutions, find the closest. */
		int delta_x = int(desired_width) - int(visible_width);
		int delta_y = int(desired_height) - int(visible_height);
		int old_delta_x = int(desired_width) - int(*width);
		int old_delta_y = int(desired_height) - int(*height);

		int dist = delta_x * delta_x + delta_y * delta_y;
		int old_dist = old_delta_x * old_delta_x + old_delta_y * old_delta_y;
		if (dist < old_dist)
		{
			*width = visible_width;
			*height = visible_height;
			return true;
		}
		else
			return false;
	}
}
#endif

bool WSI::init(unsigned width, unsigned height)
{
	const char *device_ext = "VK_KHR_swapchain";

#if defined(HAVE_GLFW)
	if (!glfwInit())
		return false;

	if (!Context::init_loader(glfwGetInstanceProcAddress))
		return false;

	uint32_t count;
	const char **ext = glfwGetRequiredInstanceExtensions(&count);
	context = unique_ptr<Context>(new Context(ext, count, &device_ext, 1));

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	//auto *monitor = glfwGetPrimaryMonitor();
	//auto *mode = glfwGetVideoMode(monitor);
	window = glfwCreateWindow(width, height, "GLFW Window", nullptr, nullptr);
	if (glfwCreateWindowSurface(context->get_instance(), window, nullptr, &surface) != VK_SUCCESS)
		return false;

	int actual_width, actual_height;
	glfwGetFramebufferSize(window, &actual_width, &actual_height);
	width = unsigned(actual_width);
	height = unsigned(actual_height);
#elif defined(HAVE_KHR_DISPLAY)
	if (!Context::init_loader(nullptr))
		return false;

	static const char *instance_ext[] = {
		"VK_KHR_surface", "VK_KHR_display",
	};
	context =
	    unique_ptr<Context>(new Context(instance_ext, sizeof(instance_ext) / sizeof(instance_ext[0]), &device_ext, 1));

	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceDisplayPropertiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceDisplayPlanePropertiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetDisplayPlaneSupportedDisplaysKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetDisplayModePropertiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkCreateDisplayModeKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetDisplayPlaneCapabilitiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkCreateDisplayPlaneSurfaceKHR);

	uint32_t display_count;
	vkGetPhysicalDeviceDisplayPropertiesKHR(context->get_gpu(), &display_count, nullptr);
	vector<VkDisplayPropertiesKHR> displays(display_count);
	vkGetPhysicalDeviceDisplayPropertiesKHR(context->get_gpu(), &display_count, displays.data());

	uint32_t plane_count;
	vkGetPhysicalDeviceDisplayPlanePropertiesKHR(context->get_gpu(), &plane_count, nullptr);
	vector<VkDisplayPlanePropertiesKHR> planes(plane_count);
	vkGetPhysicalDeviceDisplayPlanePropertiesKHR(context->get_gpu(), &plane_count, planes.data());

	VkDisplayModeKHR best_mode = VK_NULL_HANDLE;
	uint32_t best_plane = UINT32_MAX;

	unsigned actual_width = 0;
	unsigned actual_height = 0;
	VkDisplayPlaneAlphaFlagBitsKHR alpha_mode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;

	for (unsigned dpy = 0; dpy < display_count; dpy++)
	{
		VkDisplayKHR display = displays[dpy].display;
		best_mode = VK_NULL_HANDLE;
		best_plane = UINT32_MAX;

		uint32_t mode_count;
		vkGetDisplayModePropertiesKHR(context->get_gpu(), display, &mode_count, nullptr);
		vector<VkDisplayModePropertiesKHR> modes(mode_count);
		vkGetDisplayModePropertiesKHR(context->get_gpu(), display, &mode_count, modes.data());

		for (unsigned i = 0; i < mode_count; i++)
		{
			const VkDisplayModePropertiesKHR &mode = modes[i];
			if (vulkan_update_display_mode(&actual_width, &actual_height, &mode, 0, 0))
				best_mode = mode.displayMode;
		}

		if (best_mode == VK_NULL_HANDLE)
			continue;

		for (unsigned i = 0; i < plane_count; i++)
		{
			uint32_t supported_count = 0;
			VkDisplayPlaneCapabilitiesKHR plane_caps;
			vkGetDisplayPlaneSupportedDisplaysKHR(context->get_gpu(), i, &supported_count, nullptr);

			if (!supported_count)
				continue;

			vector<VkDisplayKHR> supported(supported_count);
			vkGetDisplayPlaneSupportedDisplaysKHR(context->get_gpu(), i, &supported_count, supported.data());

			unsigned j;
			for (j = 0; j < supported_count; j++)
			{
				if (supported[j] == display)
				{
					if (best_plane == UINT32_MAX)
						best_plane = j;
					break;
				}
			}

			if (j == supported_count)
				continue;

			if (planes[i].currentDisplay == VK_NULL_HANDLE || planes[i].currentDisplay == display)
				best_plane = j;
			else
				continue;

			vkGetDisplayPlaneCapabilitiesKHR(context->get_gpu(), best_mode, i, &plane_caps);

			if (plane_caps.supportedAlpha & VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR)
			{
				best_plane = j;
				alpha_mode = VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
				goto out;
			}
		}
	}
out:

	if (best_mode == VK_NULL_HANDLE)
		return false;
	if (best_plane == UINT32_MAX)
		return false;

	VkDisplaySurfaceCreateInfoKHR create_info = { VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR };
	create_info.displayMode = best_mode;
	create_info.planeIndex = best_plane;
	create_info.planeStackIndex = planes[best_plane].currentStackIndex;
	create_info.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	create_info.globalAlpha = 1.0f;
	create_info.alphaMode = (VkDisplayPlaneAlphaFlagBitsKHR)alpha_mode;
	create_info.imageExtent.width = width;
	create_info.imageExtent.height = height;

	if (vkCreateDisplayPlaneSurfaceKHR(context->get_instance(), &create_info, NULL, &surface) != VK_SUCCESS)
		return false;
#elif defined(HAVE_ANDROID_SURFACE)
	if (!native_window)
		return false;

	PFN_vkCreateAndroidSurfaceKHR create_surface;
	if (!Context::init_loader(nullptr))
		return false;

	static const char *instance_ext[] = {
			"VK_KHR_surface", "VK_KHR_android_surface",
	};
	context =
			unique_ptr<Context>(new Context(instance_ext, sizeof(instance_ext) / sizeof(instance_ext[0]), &device_ext, 1));

	if (!VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_SYMBOL(context->get_instance(), "vkCreateAndroidSurfaceKHR", create_surface))
		return false;

	VkAndroidSurfaceCreateInfoKHR surface_info = { VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR };
	surface_info.window = native_window;
	if (create_surface(context->get_instance(), &surface_info, nullptr, &surface) != VK_SUCCESS)
		return false;
#endif

	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkDestroySurfaceKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceSupportKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(), vkGetPhysicalDeviceSurfaceFormatsKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_INSTANCE_EXTENSION_SYMBOL(context->get_instance(),
	                                                     vkGetPhysicalDeviceSurfacePresentModesKHR);

	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkCreateSwapchainKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkDestroySwapchainKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkGetSwapchainImagesKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkAcquireNextImageKHR);
	VULKAN_SYMBOL_WRAPPER_LOAD_DEVICE_EXTENSION_SYMBOL(context->get_device(), vkQueuePresentKHR);

	VkBool32 supported = false;
	vkGetPhysicalDeviceSurfaceSupportKHR(context->get_gpu(), context->get_queue_family(), surface, &supported);
	if (!supported)
		return false;

	if (!init_swapchain(width, height))
		return false;

#if defined(HAVE_GLFW)
	glfwSetWindowUserPointer(window, this);
	glfwSetFramebufferSizeCallback(window, fb_size_cb);
	glfwSetKeyCallback(window, key_cb);
	glfwSetMouseButtonCallback(window, button_cb);
	glfwSetCursorPosCallback(window, cursor_cb);
	glfwSetCursorEnterCallback(window, enter_cb);
#endif

	semaphore_manager.init(context->get_device());
	device.set_context(*context);
	auto &em = Granite::EventManager::get_global();
	em.enqueue_latched<DeviceCreatedEvent>(&device);

	device.init_swapchain(swapchain_images, width, height, format);
	this->width = width;
	this->height = height;

	return true;
}

bool WSI::begin_frame()
{
	if (!need_acquire)
		return true;

	VkResult result;
	do
	{
		VkSemaphore acquire = semaphore_manager.request_cleared_semaphore();
		result = vkAcquireNextImageKHR(context->get_device(), swapchain, UINT64_MAX, acquire, VK_NULL_HANDLE,
		                               &swapchain_index);

		if (result == VK_SUCCESS)
		{
			auto &em = Granite::EventManager::get_global();
			auto frame_time = timer.frame();
			auto elapsed_time = timer.get_elapsed();

			// Poll after acquire as well for optimal latency.
			poll_input();
			em.dispatch_inline(FrameTickEvent{frame_time, elapsed_time});

			release_semaphore = semaphore_manager.request_cleared_semaphore();
			device.begin_frame(swapchain_index);
			em.dequeue_all_latched(SwapchainIndexEvent::type_id);
			em.enqueue_latched<SwapchainIndexEvent>(&device, swapchain_index);
			semaphore_manager.recycle(device.set_acquire(acquire));
			semaphore_manager.recycle(device.set_release(release_semaphore));
		}
		else if (result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_OUT_OF_DATE_KHR)
		{
			VK_ASSERT(width != 0);
			VK_ASSERT(height != 0);
			vkDeviceWaitIdle(device.get_device());
			vkDestroySemaphore(device.get_device(), acquire, nullptr);

			auto old_acquire = device.set_acquire(VK_NULL_HANDLE);
			auto old_release = device.set_release(VK_NULL_HANDLE);
			if (old_acquire != VK_NULL_HANDLE)
				vkDestroySemaphore(device.get_device(), old_acquire, nullptr);
			if (old_release != VK_NULL_HANDLE)
				vkDestroySemaphore(device.get_device(), old_release, nullptr);

			if (!init_swapchain(width, height))
				return false;
			device.init_swapchain(swapchain_images, width, height, format);
		}
		else
		{
			semaphore_manager.recycle(acquire);
			return false;
		}
	} while (result != VK_SUCCESS);
	return true;
}

bool WSI::end_frame()
{
	device.flush_frame();

	if (!device.swapchain_touched())
	{
		need_acquire = false;
		device.wait_idle();
		return true;
	}

	need_acquire = true;

	VkResult result = VK_SUCCESS;
	VkPresentInfoKHR info = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	info.waitSemaphoreCount = 1;
	info.pWaitSemaphores = &release_semaphore;
	info.swapchainCount = 1;
	info.pSwapchains = &swapchain;
	info.pImageIndices = &swapchain_index;
	info.pResults = &result;

	VkResult overall = vkQueuePresentKHR(context->get_queue(), &info);
	if (overall != VK_SUCCESS || result != VK_SUCCESS)
	{
		LOGE("vkQueuePresentKHR failed.\n");
		return false;
	}
	return true;
}

void WSI::update_framebuffer(unsigned width, unsigned height)
{
	vkDeviceWaitIdle(context->get_device());
	init_swapchain(width, height);
	device.init_swapchain(swapchain_images, width, height, format);
}

bool WSI::init_swapchain(unsigned width, unsigned height)
{
	VkSurfaceCapabilitiesKHR surface_properties;
	auto gpu = context->get_gpu();
	V(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &surface_properties));

	uint32_t format_count;
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, nullptr);
	vector<VkSurfaceFormatKHR> formats(format_count);
	vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &format_count, formats.data());

	VkSurfaceFormatKHR format;
	if (format_count == 1 && formats[0].format == VK_FORMAT_UNDEFINED)
	{
		format = formats[0];
		format.format = VK_FORMAT_B8G8R8A8_UNORM;
	}
	else
	{
		if (format_count == 0)
		{
			LOGE("Surface has no formats.\n");
			return false;
		}

		bool found = false;
		for (unsigned i = 0; i < format_count; i++)
		{
			if (formats[i].format == VK_FORMAT_R8G8B8A8_SRGB || formats[i].format == VK_FORMAT_B8G8R8A8_SRGB)
			{
				format = formats[i];
				found = true;
			}
		}

		if (!found)
			format = formats[0];
	}

	VkExtent2D swapchain_size;
	if (surface_properties.currentExtent.width == -1u)
	{
		swapchain_size.width = width;
		swapchain_size.height = height;
	}
	else
		swapchain_size = surface_properties.currentExtent;

	uint32_t num_present_modes;
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, nullptr);
	vector<VkPresentModeKHR> present_modes(num_present_modes);
	vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &num_present_modes, present_modes.data());

	VkPresentModeKHR swapchain_present_mode = VK_PRESENT_MODE_FIFO_KHR;
#if 0
	for (uint32_t i = 0; i < num_present_modes; i++)
	{
		if (present_modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR || present_modes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
		{
			swapchain_present_mode = present_modes[i];
			break;
		}
	}
#endif

	uint32_t desired_swapchain_images = surface_properties.minImageCount + 1;
	if ((surface_properties.maxImageCount > 0) && (desired_swapchain_images > surface_properties.maxImageCount))
		desired_swapchain_images = surface_properties.maxImageCount;

	VkSurfaceTransformFlagBitsKHR pre_transform;
	if (surface_properties.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
		pre_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	else
		pre_transform = surface_properties.currentTransform;

	VkSwapchainKHR old_swapchain = swapchain;

	VkSwapchainCreateInfoKHR info = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
	info.surface = surface;
	info.minImageCount = desired_swapchain_images;
	info.imageFormat = format.format;
	info.imageColorSpace = format.colorSpace;
	info.imageExtent.width = swapchain_size.width;
	info.imageExtent.height = swapchain_size.height;
	info.imageArrayLayers = 1;
	info.imageUsage =
	    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	info.preTransform = pre_transform;
	info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	info.presentMode = swapchain_present_mode;
	info.clipped = true;
	info.oldSwapchain = old_swapchain;

	V(vkCreateSwapchainKHR(context->get_device(), &info, nullptr, &swapchain));

	if (old_swapchain != VK_NULL_HANDLE)
		vkDestroySwapchainKHR(context->get_device(), old_swapchain, nullptr);

	this->width = swapchain_size.width;
	this->height = swapchain_size.height;
	this->format = format.format;

	LOGI("Created swapchain %u x %u (fmt: %u).\n", this->width, this->height, static_cast<unsigned>(this->format));

	uint32_t image_count;
	V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, nullptr));
	swapchain_images.resize(image_count);
	V(vkGetSwapchainImagesKHR(context->get_device(), swapchain, &image_count, swapchain_images.data()));

	auto &em = Granite::EventManager::get_global();
	em.dequeue_all_latched(SwapchainParameterEvent::type_id);
	em.enqueue_latched<SwapchainParameterEvent>(&device, this->width, this->height, image_count, info.imageFormat);

	return true;
}

WSI::~WSI()
{
	auto &em = Granite::EventManager::get_global();
	if (context)
	{
		vkDeviceWaitIdle(context->get_device());
		semaphore_manager.recycle(device.set_acquire(VK_NULL_HANDLE));
		semaphore_manager.recycle(device.set_release(VK_NULL_HANDLE));
		if (swapchain != VK_NULL_HANDLE)
		{
			em.dequeue_all_latched(SwapchainParameterEvent::type_id);
			vkDestroySwapchainKHR(context->get_device(), swapchain, nullptr);
		}
	}

#if defined(HAVE_GLFW)
	if (window)
		glfwDestroyWindow(window);
#endif

	if (surface != VK_NULL_HANDLE)
		vkDestroySurfaceKHR(context->get_instance(), surface, nullptr);

	em.dequeue_all_latched(DeviceCreatedEvent::type_id);
}
}
