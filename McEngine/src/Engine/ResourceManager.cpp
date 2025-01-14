//================ Copyright (c) 2015, PG, All rights reserved. =================//
//
// Purpose:		resource manager
//
// $NoKeywords: $rm
//===============================================================================//

#include "ResourceManager.h"
#include "Engine.h"
#include "ConVar.h"
#include "Timer.h"



// HACKHACK: until I get around to writing an std::thread wrapper implementation
#ifdef __SWITCH__

#include <switch.h>

#endif



#ifdef MCENGINE_FEATURE_MULTITHREADING

#include <mutex>
#include "WinMinGW.Mutex.h"
#include "Horizon.Mutex.h"

#endif

ConVar rm_warnings("rm_warnings", false);
ConVar rm_async_rand_delay("rm_async_rand_delay", 0.0f);
ConVar rm_interrupt_on_destroy("rm_interrupt_on_destroy", true);
ConVar debug_rm("debug_rm", false);

// TODO: rewrite this garbage. support any number of parallel resource loading threads (default to 2)
#ifdef MCENGINE_FEATURE_MULTITHREADING

extern bool g_bRunning;

std::mutex g_resourceManagerMutex;
std::mutex g_resourceManagerLoadingMutex;
std::mutex g_resourceManagerLoadingWorkMutex;

//void *_resourceLoadThread(void *data);
void *_resourceLoaderThread(void *data);
void _resourceLoaderThreadVoid(void *data);

#endif



// HACKHACK: do this with env->getOS() or something
#ifdef __SWITCH__

const char *ResourceManager::PATH_DEFAULT_IMAGES = "romfs:/materials/";
const char *ResourceManager::PATH_DEFAULT_FONTS = "romfs:/fonts/";
const char *ResourceManager::PATH_DEFAULT_SOUNDS = "romfs:/sounds/";
const char *ResourceManager::PATH_DEFAULT_SHADERS = "romfs:/shaders/";

#else

const char *ResourceManager::PATH_DEFAULT_IMAGES = "materials/";
const char *ResourceManager::PATH_DEFAULT_FONTS = "fonts/";
const char *ResourceManager::PATH_DEFAULT_SOUNDS = "sounds/";
const char *ResourceManager::PATH_DEFAULT_SHADERS = "shaders/";

#endif




ResourceManager::ResourceManager()
{
	m_bNextLoadAsync = false;

#ifdef MCENGINE_FEATURE_MULTITHREADING

	// stop loading thread, wait for work
	g_resourceManagerLoadingMutex.lock();

#endif

	// build loading thread
#ifdef MCENGINE_FEATURE_PTHREADS

	int ret = pthread_create(&m_loadingThread, NULL, _resourceLoaderThread, (void*)&m_loadingWork);
	if (ret)
		engine->showMessageError("ResourceManager Error", UString::format("pthread_create() returned %i!", ret));

#elif defined(__SWITCH__)

	Result rc = threadCreate((Thread*)&m_loadingThread, _resourceLoaderThreadVoid, (void*)&m_loadingWork, 0x1000000, 0x2B, 2);
	if (R_FAILED(rc))
		engine->showMessageError("ResourceManager Error", UString::format("threadCreate() returned %i!", (int)rc));
	else
		threadStart((Thread*)&m_loadingThread);

#endif
}

ResourceManager::~ResourceManager()
{
	// release all resources
	destroyResources();

#ifdef MCENGINE_FEATURE_MULTITHREADING

	if (m_loadingWork.size() < 1)
		g_resourceManagerLoadingMutex.unlock(); // let it exit

#endif

#ifdef MCENGINE_FEATURE_PTHREADS

	pthread_join(m_loadingThread, NULL); // TODO: not the best solution. will block shutdown until all loading work is done

#elif defined(__SWITCH__)

	threadWaitForExit((Thread*)&m_loadingThread);
	threadClose((Thread*)&m_loadingThread);

#endif
}

void ResourceManager::update()
{
#ifdef MCENGINE_FEATURE_MULTITHREADING

	if (debug_rm.getBool())
	{
		if (m_threads.size() > 0)
			debugLog("Resource Manager: %i active worker thread(s)\n", m_threads.size());
	}

#endif

	bool reLock = false;

#ifdef MCENGINE_FEATURE_MULTITHREADING

	g_resourceManagerMutex.lock();

#endif
	{
		// handle load finish (and synchronous init())
		for (int i=0; i<m_loadingWork.size(); i++)
		{
			if (m_loadingWork[i].second.atomic)
			{
				if (debug_rm.getBool())
					debugLog("Resource Manager: Worker thread #%i finished.\n", i);

				// copy pointer, so we can stop everything before finishing
				Resource *rs = m_loadingWork[i].first;

#ifdef MCENGINE_FEATURE_MULTITHREADING

				g_resourceManagerLoadingWorkMutex.lock();

#endif

				{
					m_loadingWork.erase(m_loadingWork.begin()+i);
				}

#ifdef MCENGINE_FEATURE_MULTITHREADING

				g_resourceManagerLoadingWorkMutex.unlock();

#endif

				i--;

#ifdef MCENGINE_FEATURE_MULTITHREADING

				// stop the worker thread if everything has been loaded
				if (m_loadingWork.size() < 1)
					g_resourceManagerLoadingMutex.lock();

				// unlock. this allows resources to trigger "recursive" loads within init()
				g_resourceManagerMutex.unlock();

#endif

				reLock = true;

				// finish (synchronous init())
				rs->load();

				break; // only allow 1 work item to finish per tick
			}
		}

#ifdef MCENGINE_FEATURE_MULTITHREADING

	if (reLock)
	{
		g_resourceManagerMutex.lock();
	}

#endif

		// handle async destroy
		for (int i=0; i<m_loadingWorkAsyncDestroy.size(); i++)
		{
			bool canBeDestroyed = true;
			for (int w=0; w<m_loadingWork.size(); w++)
			{
				if (m_loadingWork[w].first == m_loadingWorkAsyncDestroy[i])
				{
					if (debug_rm.getBool())
						debugLog("Resource Manager: Waiting for async destroy of #%i ...\n", i);

					canBeDestroyed = false;
					break;
				}
			}

			if (canBeDestroyed)
			{
				if (debug_rm.getBool())
					debugLog("Resource Manager: Async destroy of #%i\n", i);

				delete m_loadingWorkAsyncDestroy[i]; // implicitly calls release() through the Resource destructor
				m_loadingWorkAsyncDestroy.erase(m_loadingWorkAsyncDestroy.begin()+i);
				i--;
			}
		}
	}
#ifdef MCENGINE_FEATURE_MULTITHREADING

	g_resourceManagerMutex.unlock();

#endif
}

void ResourceManager::destroyResources()
{
	while (m_vResources.size() > 0)
	{
		destroyResource(m_vResources[0]);
	}
	m_vResources.clear();
}

void ResourceManager::destroyResource(Resource *rs)
{
	if (rs == NULL)
	{
		if (rm_warnings.getBool())
			debugLog("RESOURCE MANAGER Warning: destroyResource(NULL)!\n");
		return;
	}

	if (debug_rm.getBool())
		debugLog("ResourceManager: Destroying %s\n", rs->getName().toUtf8());

#ifdef MCENGINE_FEATURE_MULTITHREADING

	g_resourceManagerMutex.lock();

#endif
	{
		bool isManagedResource = false;
		int managedResourceIndex = -1;
		for (int i=0; i<m_vResources.size(); i++)
		{
			if (m_vResources[i] == rs)
			{
				isManagedResource = true;
				managedResourceIndex = i;
				break;
			}
		}

		// handle async destroy
		for (int w=0; w<m_loadingWork.size(); w++)
		{
			if (m_loadingWork[w].first == rs)
			{
				if (debug_rm.getBool())
					debugLog("Resource Manager: Scheduled async destroy of %s\n", rs->getName().toUtf8());

				if (rm_interrupt_on_destroy.getBool())
					rs->interruptLoad();

				m_loadingWorkAsyncDestroy.push_back(rs);
				if (isManagedResource)
					m_vResources.erase(m_vResources.begin()+managedResourceIndex);

				// HACKHACK: ugly
#ifdef MCENGINE_FEATURE_MULTITHREADING

				g_resourceManagerMutex.unlock();

#endif
				return; // we're done here
			}
		}

		// standard destroy
		SAFE_DELETE(rs); // implicitly calls release() through the Resource destructor
		if (isManagedResource)
			m_vResources.erase(m_vResources.begin()+managedResourceIndex);
	}
#ifdef MCENGINE_FEATURE_MULTITHREADING

	g_resourceManagerMutex.unlock();

#endif
}

void ResourceManager::reloadResources()
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		m_vResources[i]->reload();
	}
}

void ResourceManager::requestNextLoadAsync()
{
	m_bNextLoadAsync = true;
}

void ResourceManager::requestNextLoadUnmanaged()
{
	m_nextLoadUnmanagedStack.push(true);
}

Image *ResourceManager::loadImage(UString filepath, UString resourceName, bool mipmapped, bool keepInSystemMemory)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Image*>(temp);
	}

	// create instance and load it
	filepath.insert(0, PATH_DEFAULT_IMAGES);
	Image *img = engine->getGraphics()->createImage(filepath, mipmapped, keepInSystemMemory);
	img->setName(resourceName);

	loadResource(img, true);

	return img;
}

Image *ResourceManager::loadImageUnnamed(UString filepath, bool mipmapped, bool keepInSystemMemory)
{
	// create instance and load it
	filepath.insert(0, PATH_DEFAULT_IMAGES);
	Image *img = engine->getGraphics()->createImage(filepath, mipmapped, keepInSystemMemory);

	loadResource(img, true);

	return img;
}

Image *ResourceManager::loadImageAbs(UString absoluteFilepath, UString resourceName, bool mipmapped, bool keepInSystemMemory)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Image*>(temp);
	}

	// create instance and load it
	Image *img = engine->getGraphics()->createImage(absoluteFilepath, mipmapped, keepInSystemMemory);
	img->setName(resourceName);

	loadResource(img, true);

	return img;
}

Image *ResourceManager::loadImageAbsUnnamed(UString absoluteFilepath, bool mipmapped, bool keepInSystemMemory)
{
	// create instance and load it
	Image *img = engine->getGraphics()->createImage(absoluteFilepath, mipmapped, keepInSystemMemory);

	loadResource(img, true);

	return img;
}

Image *ResourceManager::createImage(unsigned int width, unsigned int height, bool mipmapped, bool keepInSystemMemory)
{
	if (width < 1 || height < 1 || width > 4096 || height > 4096)
	{
		engine->showMessageError("Resource Manager Error", UString::format("Invalid parameters in createImage(%i, %i, %i)!\n", width, height, (int)mipmapped));
		return NULL;
	}

	// create instance and load it
	Image *img = engine->getGraphics()->createImage(width, height, mipmapped, keepInSystemMemory);
	img->setName("<CREATED_IMAGE>");

	loadResource(img, false);

	return img;
}

McFont *ResourceManager::loadFont(UString filepath, UString resourceName, int fontSize, bool antialiasing, int fontDPI)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<McFont*>(temp);
	}

	// create instance and load it
	filepath.insert(0, PATH_DEFAULT_FONTS);
	McFont *fnt = new McFont(filepath, fontSize, antialiasing, fontDPI);
	fnt->setName(resourceName);

	loadResource(fnt, true);

	return fnt;
}

McFont *ResourceManager::loadFont(UString filepath, UString resourceName, std::vector<wchar_t> characters, int fontSize, bool antialiasing, int fontDPI)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<McFont*>(temp);
	}

	// create instance and load it
	filepath.insert(0, PATH_DEFAULT_FONTS);
	McFont *fnt = new McFont(filepath, characters, fontSize, antialiasing, fontDPI);
	fnt->setName(resourceName);

	loadResource(fnt, true);

	return fnt;
}

Sound *ResourceManager::loadSound(UString filepath, UString resourceName, bool stream, bool threeD, bool loop, bool prescan)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Sound*>(temp);
	}

	// create instance and load it
	filepath.insert(0, PATH_DEFAULT_SOUNDS);
	Sound *snd = new Sound(filepath, stream, threeD, loop, prescan);
	snd->setName(resourceName);

	loadResource(snd, true);

	return snd;
}

Sound *ResourceManager::loadSoundAbs(UString filepath, UString resourceName, bool stream, bool threeD, bool loop, bool prescan)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Sound*>(temp);
	}

	// create instance and load it
	Sound *snd = new Sound(filepath, stream, threeD, loop, prescan);
	snd->setName(resourceName);

	loadResource(snd, true);

	return snd;
}

Shader *ResourceManager::loadShader(UString vertexShaderFilePath, UString fragmentShaderFilePath, UString resourceName)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Shader*>(temp);
	}

	// create instance and load it
	vertexShaderFilePath.insert(0, PATH_DEFAULT_SHADERS);
	fragmentShaderFilePath.insert(0, PATH_DEFAULT_SHADERS);
	Shader *shader = engine->getGraphics()->createShaderFromFile(vertexShaderFilePath, fragmentShaderFilePath);
	shader->setName(resourceName);

	loadResource(shader, true);

	return shader;
}

Shader *ResourceManager::loadShader(UString vertexShaderFilePath, UString fragmentShaderFilePath)
{
	// create instance and load it
	vertexShaderFilePath.insert(0, PATH_DEFAULT_SHADERS);
	fragmentShaderFilePath.insert(0, PATH_DEFAULT_SHADERS);
	Shader *shader = engine->getGraphics()->createShaderFromFile(vertexShaderFilePath, fragmentShaderFilePath);

	loadResource(shader, true);

	return shader;
}

Shader *ResourceManager::createShader(UString vertexShader, UString fragmentShader, UString resourceName)
{
	// check if it already exists
	if (resourceName.length() > 0)
	{
		Resource *temp = existsAndHandle(resourceName);
		if (temp != NULL)
			return dynamic_cast<Shader*>(temp);
	}

	// create instance and load it
	Shader *shader = engine->getGraphics()->createShaderFromSource(vertexShader, fragmentShader);
	shader->setName(resourceName);

	loadResource(shader, true);

	return shader;
}

Shader *ResourceManager::createShader(UString vertexShader, UString fragmentShader)
{
	// create instance and load it
	Shader *shader = engine->getGraphics()->createShaderFromSource(vertexShader, fragmentShader);

	loadResource(shader, true);

	return shader;
}

RenderTarget *ResourceManager::createRenderTarget(int x, int y, int width, int height, Graphics::MULTISAMPLE_TYPE multiSampleType)
{
	// create instance and load it
	RenderTarget *rt = engine->getGraphics()->createRenderTarget(x, y, width, height, multiSampleType);
	rt->setName(UString::format("<RT_(%ix%i)>", width, height));

	loadResource(rt, true);

	return rt;
}

RenderTarget *ResourceManager::createRenderTarget(int width, int height, Graphics::MULTISAMPLE_TYPE multiSampleType)
{
	return createRenderTarget(0, 0, width, height, multiSampleType);
}

TextureAtlas *ResourceManager::createTextureAtlas(int width, int height)
{
	// create instance and load it
	TextureAtlas *ta = new TextureAtlas(width, height);
	ta->setName(UString::format("<TA_(%ix%i)>", width, height));

	loadResource(ta, false);

	return ta;
}

VertexArrayObject *ResourceManager::createVertexArrayObject(Graphics::PRIMITIVE primitive, Graphics::USAGE_TYPE usage, bool keepInSystemMemory)
{
	// create instance and load it
	VertexArrayObject *vao = engine->getGraphics()->createVertexArrayObject(primitive, usage, keepInSystemMemory);

	loadResource(vao, false); // doesn't need to be loaded, since vaos will be filled by the user and then engine->getResourceManager()->loadResource()'d later

	return vao;
}

Image *ResourceManager::getImage(UString resourceName)
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		if (m_vResources[i]->getName() == resourceName)
			return dynamic_cast<Image*>(m_vResources[i]);
	}

	doesntExistWarning(resourceName);
	return NULL;
}

McFont *ResourceManager::getFont(UString resourceName)
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		if (m_vResources[i]->getName() == resourceName)
			return dynamic_cast<McFont*>(m_vResources[i]);
	}

	doesntExistWarning(resourceName);
	return NULL;
}

Sound *ResourceManager::getSound(UString resourceName)
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		if (m_vResources[i]->getName() == resourceName)
			return dynamic_cast<Sound*>(m_vResources[i]);
	}

	doesntExistWarning(resourceName);
	return NULL;
}

Shader *ResourceManager::getShader(UString resourceName)
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		if (m_vResources[i]->getName() == resourceName)
			return dynamic_cast<Shader*>(m_vResources[i]);
	}

	doesntExistWarning(resourceName);
	return NULL;
}

bool ResourceManager::isLoadingResource(Resource *rs) const
{
	for (int i=0; i<m_loadingWork.size(); i++)
	{
		if (m_loadingWork[i].first == rs)
			return true;
	}

	return false;
}

void ResourceManager::loadResource(Resource *res, bool load)
{
	// handle flags
	if (m_nextLoadUnmanagedStack.size() < 1 || !m_nextLoadUnmanagedStack.top())
		m_vResources.push_back(res); // add managed resource

	const bool isNextLoadAsync = m_bNextLoadAsync;

	// flags must be reset on every load, to not carry over
	resetFlags();

	if (!load) return;

	if (!isNextLoadAsync)
	{
		// load normally
		res->loadAsync();
		res->load();
	}
	else
	{
#if defined(MCENGINE_FEATURE_MULTITHREADING)

		g_resourceManagerMutex.lock();
		{
			// add work to loading thread
			std::pair<Resource*, MobileAtomicBool> work;
			work.first = res;
			work.second = MobileAtomic<bool>(false);

			g_resourceManagerLoadingWorkMutex.lock();
			m_loadingWork.push_back(work);

			// let the loading thread run
			if (m_loadingWork.size() == 1)
				g_resourceManagerLoadingMutex.unlock();

			g_resourceManagerLoadingWorkMutex.unlock();
		}
		g_resourceManagerMutex.unlock();

#else

		// load normally (on platforms which don't support multithreading)
		res->loadAsync();
		res->load();

#endif
	}
}

void ResourceManager::doesntExistWarning(UString resourceName)
{
	if (rm_warnings.getBool())
	{
		UString errormsg = "Resource \"";
		errormsg.append(resourceName);
		errormsg.append("\" does not exist!");
		engine->showMessageWarning("RESOURCE MANAGER: ", errormsg);
	}
}

Resource *ResourceManager::existsAndHandle(UString resourceName)
{
	for (int i=0; i<m_vResources.size(); i++)
	{
		if (m_vResources[i]->getName() == resourceName)
		{
			if (rm_warnings.getBool())
				debugLog("RESOURCE MANAGER: Resource \"%s\" already loaded!\n", resourceName.toUtf8());

			// handle flags (reset them)
			resetFlags();

			return m_vResources[i];
		}
	}

	return NULL;
}

void ResourceManager::resetFlags()
{
	if (m_nextLoadUnmanagedStack.size() > 0)
		m_nextLoadUnmanagedStack.pop();

	m_bNextLoadAsync = false;
}



#ifdef MCENGINE_FEATURE_MULTITHREADING

/*
void *_resourceLoadThread(void *data)
{
	// debugging
	if (rm_async_rand_delay.getInt() > 0)
	{
		Timer sleepTimer;
		sleepTimer.start();
		double randSleepSecs = rand() % rm_async_rand_delay.getInt();
		while (sleepTimer.getElapsedTime() < randSleepSecs)
		{
			sleepTimer.update();
		}
	}

	ResourceManager::LOAD_THREAD *loader = (ResourceManager::LOAD_THREAD*)data;
	loader->resource->loadAsync();
	loader->finished = ResourceManager::MobileAtomic<bool>(true);
	return NULL;
}
*/

void *_resourceLoaderThread(void *data)
{
	std::vector<std::pair<Resource*, ResourceManager::MobileAtomicBool>> *todo = (std::vector<std::pair<Resource*, ResourceManager::MobileAtomicBool>>*)data;

	while (g_bRunning)
	{
		// wait for work
		g_resourceManagerLoadingMutex.lock(); // thread will wait here
		g_resourceManagerLoadingMutex.unlock();

		int size = 0;
		std::pair<Resource*, ResourceManager::MobileAtomicBool> work;

		// quickly check if there is work to do (this can potentially cause engine lag!)
		g_resourceManagerLoadingWorkMutex.lock();
		{
			size = todo->size();
			if (size > 0)
				work = (*todo)[0];
		}
		g_resourceManagerLoadingWorkMutex.unlock();

		// if we have work
		if (size > 0)
		{
			if (!(work.second.atomic)) // if we need to get loaded
			{
				// debugging
				if (rm_async_rand_delay.getInt() > 0)
				{
					Timer sleepTimer;
					sleepTimer.start();
					double randSleepSecs = rand() % rm_async_rand_delay.getInt();
					while (sleepTimer.getElapsedTime() < randSleepSecs)
					{
						sleepTimer.update();
					}
				}

				work.first->loadAsync();

				// very quickly signal that we are done
				g_resourceManagerLoadingWorkMutex.lock();
				{
					(*todo)[0].second = ResourceManager::MobileAtomic<bool>(true);
				}
				g_resourceManagerLoadingWorkMutex.unlock();
			}
		}
	}

	return NULL;
}

void _resourceLoaderThreadVoid(void *data)
{
	_resourceLoaderThread(data);
}

#endif
