//================ Copyright (c) 2015, PG, All rights reserved. =================//
//
// Purpose:		resource manager
//
// $NoKeywords: $rm
//===============================================================================//

#ifndef RESOURCEMANAGER_H
#define RESOURCEMANAGER_H

#include "cbase.h"

#include "Image.h"
#include "Font.h"
#include "Sound.h"
#include "Shader.h"
#include "RenderTarget.h"
#include "TextureAtlas.h"
#include "VertexArrayObject.h"

#ifdef MCENGINE_FEATURE_PTHREADS

#include <pthread.h>

#endif



// HACKHACK: until I get around to writing an std::thread wrapper implementation
#ifdef __SWITCH__

typedef struct {
    uint32_t handle;     ///< Thread handle.
    void*  stack_mem;    ///< Pointer to stack memory.
    void*  stack_mirror; ///< Pointer to stack memory mirror.
    size_t stack_sz;     ///< Stack size.
} HorizonThread;

#endif



class ResourceManager
{
public:
	static const char *PATH_DEFAULT_IMAGES;
	static const char *PATH_DEFAULT_FONTS;
	static const char *PATH_DEFAULT_SOUNDS;
	static const char *PATH_DEFAULT_SHADERS;

public:
	ResourceManager();
	~ResourceManager();

	void update();

	void loadResource(Resource *rs) {requestNextLoadUnmanaged(); loadResource(rs, true);}
	void destroyResource(Resource *rs);
	void destroyResources();
	void reloadResources();

	void requestNextLoadAsync();
	void requestNextLoadUnmanaged();

	// images
	Image *loadImage(UString filepath, UString resourceName, bool mipmapped = false, bool keepInSystemMemory = false);
	Image *loadImageUnnamed(UString filepath, bool mipmapped = false, bool keepInSystemMemory = false);
	Image *loadImageAbs(UString absoluteFilepath, UString resourceName, bool mipmapped = false, bool keepInSystemMemory = false);
	Image *loadImageAbsUnnamed(UString absoluteFilepath, bool mipmapped = false, bool keepInSystemMemory = false);
	Image *createImage(unsigned int width, unsigned int height, bool mipmapped = false, bool keepInSystemMemory = false);

	// fonts
	McFont *loadFont(UString filepath, UString resourceName, int fontSize = 16, bool antialiasing = true, int fontDPI = 96);
	McFont *loadFont(UString filepath, UString resourceName, std::vector<wchar_t> characters, int fontSize = 16, bool antialiasing = true, int fontDPI = 96);

	// sounds
	Sound *loadSound(UString filepath, UString resourceName, bool stream = false, bool threeD = false, bool loop = false, bool prescan = false);
	Sound *loadSoundAbs(UString filepath, UString resourceName, bool stream = false, bool threeD = false, bool loop = false, bool prescan = false);

	// shaders
	Shader *loadShader(UString vertexShaderFilePath, UString fragmentShaderFilePath, UString resourceName);
	Shader *loadShader(UString vertexShaderFilePath, UString fragmentShaderFilePath);
	Shader *createShader(UString vertexShader, UString fragmentShader, UString resourceName);
	Shader *createShader(UString vertexShader, UString fragmentShader);

	// rendertargets
	RenderTarget *createRenderTarget(int x, int y, int width, int height, Graphics::MULTISAMPLE_TYPE multiSampleType = Graphics::MULTISAMPLE_TYPE::MULTISAMPLE_0X);
	RenderTarget *createRenderTarget(int width, int height, Graphics::MULTISAMPLE_TYPE multiSampleType = Graphics::MULTISAMPLE_TYPE::MULTISAMPLE_0X);

	// texture atlas
	TextureAtlas *createTextureAtlas(int width, int height);

	// models/meshes
	VertexArrayObject *createVertexArrayObject(Graphics::PRIMITIVE primitive = Graphics::PRIMITIVE::PRIMITIVE_TRIANGLES, Graphics::USAGE_TYPE usage = Graphics::USAGE_TYPE::USAGE_STATIC, bool keepInSystemMemory = false);

	// resource access by name // TODO: should probably use generics for this
	Image *getImage(UString resourceName);
	McFont *getFont(UString resourceName);
	Sound *getSound(UString resourceName);
	Shader *getShader(UString resourceName);

	int getNumResources() const {return m_vResources.size();}
	inline std::vector<Resource*> getResources() const {return m_vResources;}

	bool isLoadingResource(Resource *rs) const;

	template<typename T>
	struct MobileAtomic
	{
		std::atomic<T> atomic;

		MobileAtomic() : atomic(T()) {}

		explicit MobileAtomic ( T const& v ) : atomic ( v ) {}
		explicit MobileAtomic ( std::atomic<T> const& a ) : atomic ( a.load() ) {}

		MobileAtomic ( MobileAtomic const&other ) : atomic( other.atomic.load() ) {}

		MobileAtomic& operator=( MobileAtomic const &other )
		{
			atomic.store( other.atomic.load() );
			return *this;
		}
	};
	typedef MobileAtomic<bool> MobileAtomicBool;

#ifdef MCENGINE_FEATURE_MULTITHREADING

	struct LOAD_THREAD
	{
#ifdef MCENGINE_FEATURE_PTHREADS

		pthread_t thread;

#elif defined(__SWITCH__)

		HorizonThread thread;

#endif

		Resource *resource;
		MobileAtomicBool finished;
	};

#endif

private:
	void loadResource(Resource *res, bool load);
	void doesntExistWarning(UString resourceName);
	Resource *existsAndHandle(UString resourceName);
	void resetFlags();

	std::vector<Resource*> m_vResources;
	std::vector<Resource*> m_vAsyncDestroy;

#ifdef MCENGINE_FEATURE_MULTITHREADING

	std::vector<LOAD_THREAD*> m_threads;

#endif

	bool m_bNextLoadAsync;
	std::stack<bool> m_nextLoadUnmanagedStack;

	std::vector<std::pair<Resource*, MobileAtomicBool>> m_loadingWork;
	std::vector<Resource*> m_loadingWorkAsyncDestroy;

#ifdef MCENGINE_FEATURE_PTHREADS

	pthread_t m_loadingThread;

#elif defined(__SWITCH__)

	HorizonThread m_loadingThread;

#endif
};

#endif
