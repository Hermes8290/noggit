// This file is part of Noggit3, licensed under GNU General Public License (version 3).

#include <noggit/AsyncLoader.h>
#include <noggit/AsyncObject.h>

#include <list>

namespace
{
  bool isFinished(AsyncObject* object)
  {
    return object->finishedLoading();
  }
}

void AsyncLoader::process()
{
  while (true)
  {
    AsyncObject* object = nextObjectToLoad();
    if (object)
    {
      object->finishLoading();
    }
    else
    {
      boost::this_thread::sleep(boost::posix_time::milliseconds(10));
    }
  }
}

AsyncObject* AsyncLoader::nextObjectToLoad()
{
  boost::mutex::scoped_lock lock(m_loadingMutex);

  m_objects.remove_if(isFinished);

  std::list<AsyncObject*>::iterator it = m_objects.begin();

  if (it == m_objects.end())
  {
    return nullptr;
  }
  else
  {
    return *it;
  }
}

void AsyncLoader::addObject(AsyncObject* _pObject)
{
  boost::mutex::scoped_lock lock(m_loadingMutex);
  m_objects.push_back(_pObject);
}

void AsyncLoader::removeObject(AsyncObject* _pObject)
{
  boost::mutex::scoped_lock lock(m_loadingMutex);
  m_objects.remove(_pObject);
}

void AsyncLoader::start(int _numThreads)
{
  for (int i = 0; i < _numThreads; ++i)
  {
    m_threads.add_thread(new boost::thread(&AsyncLoader::process, this));
  }
}

AsyncLoader::~AsyncLoader()
{
  stop();
  join();
}

void AsyncLoader::stop()
{
  m_threads.interrupt_all();
}

void AsyncLoader::join()
{
  m_threads.join_all();
}
