/*
 *
 * Copyright 2015 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <iostream>
#include <errno.h>
#include <memory>
#include <string>
#include <filesystem>
#include <sys/stat.h>
#include <openssl/sha.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdarg.h>
#include <fuse.h>
#include <grpcpp/grpcpp.h>


#ifdef BAZEL_BUILD
#include "examples/protos/helloworld.grpc.pb.h"
#else
#include "afs.grpc.pb.h"
#endif

using FS::AFS;
using FS::GetAttrRequest;
using FS::GetAttrResponse;
using FS::OpenRequest;
using FS::OpenResponse;
using grpc::Channel;
using grpc::ClientContext;
using grpc::Status;

using namespace std;

char *mountPoint;
char *cacheDir;

string sha256(const string str)
{
  unsigned char hash[SHA256_DIGEST_LENGTH];
  SHA256_CTX sha256;
  SHA256_Init(&sha256);
  SHA256_Update(&sha256, str.c_str(), str.size());
  SHA256_Final(hash, &sha256);
  stringstream ss;
  for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
  {
    ss << hex << setw(2) << setfill('0') << (int)hash[i];
  }
  return ss.str();
}

class AfsClientSingleton
{

private:
  std::unique_ptr<AFS::Stub> stub_;
  static AfsClientSingleton *instancePtr;
  string m_mountPoint;
  string m_cacheDir="/home/girish/afscache/";

  AfsClientSingleton()
  {
  }

  AfsClientSingleton(std::shared_ptr<Channel> channel) : stub_(AFS::NewStub(channel))
  {
     m_mountPoint= mountPoint;
     m_cacheDir="/home/girish/afscache/";
    //m_mountPoint = filesystem::absolute(filesystem::path(mountPoint)).string();
    //m_cacheDir = filesystem::absolute(filesystem::path(cacheDir)).string()+"/";
  }

public:
  AfsClientSingleton(const AfsClientSingleton &obj) = delete;

  static AfsClientSingleton *getInstance(std::string serverIP)
  {
    if (instancePtr == nullptr)
    {
      instancePtr = new AfsClientSingleton(grpc::CreateChannel(serverIP, grpc::InsecureChannelCredentials()));
      return instancePtr;
    }
    else
      return instancePtr;
  }

  int GetAttr(std::string path, struct stat *buf)
  {
    GetAttrRequest request;
    GetAttrResponse reply;
    ClientContext context;

    //Setting request parameters
    path = removeMountPointPrefix(path);
    request.set_path(path);

    //Trigger RPC Call for GetAttr
    Status status = stub_->GetAttr(&context, request, &reply);

    //On Response received
    if (status.ok() && reply.status() == 1)
    {
      memcpy((char *)buf, reply.statbuf().c_str(), sizeof(struct stat));
      return 1;
    }
    else
    {
      cout << status.error_code() << ": " << status.error_message() << std::endl;
      return -1;
    }
  }

  string removeMountPointPrefix(string inputPath)
  {
   
    string s_inputPath = filesystem::absolute(filesystem::path(inputPath)).string();
    cout<< "Input string :: "<< inputPath  << endl;
    cout<< "m_mountPoint :: "<< m_mountPoint  << endl;
    cout<< "m_cacheDir :: "<< m_cacheDir  << endl;

    if(s_inputPath.find(m_cacheDir) != string::npos){
      return s_inputPath.substr(m_cacheDir.size());
    }
    cout<< "Input string :: "<< inputPath  << endl;
    return inputPath;
  }

  int Open(std::string path, int flags)
  {
    path = removeMountPointPrefix(path);
    string absoluteCachePath = m_cacheDir + sha256(path);
    int fd = open(absoluteCachePath.c_str(), O_RDONLY);

    cout<< "path :: "<<path  << endl;
    cout<< "absoluteCachePath :: "<<absoluteCachePath  << endl;


    if (fd != -1)
    {
      // file in local cache, check if stale
      struct stat statBuf, lstatBuf;

      // Call getAttr to check if local file data is not stale
      GetAttr(path, &statBuf);
      memset(&lstatBuf, 0, sizeof(struct stat));
      if (lstat(absoluteCachePath.c_str(), &lstatBuf) == -1)
      {
        //return error status on failure
        // never reaches here!
        return -errno;
      }

      cout<< "statBuf.st_mtim.tv_sec :: "<< statBuf.st_mtim.tv_sec  << endl;
      cout<< "statBuf.st_mtim.tv_sec :: "<< statBuf.st_mtim.tv_nsec  << endl;

      cout<< "lstatBuf.st_mtim.tv_sec :: "<< lstatBuf.st_mtim.tv_sec  << endl;
      cout<< "lstatBuf.st_mtim.tv_sec :: "<< lstatBuf.st_mtim.tv_nsec  << endl;


      // cache not stale
      if ((statBuf.st_mtim.tv_sec < lstatBuf.st_mtim.tv_sec) || (statBuf.st_mtim.tv_sec == lstatBuf.st_mtim.tv_sec && statBuf.st_mtim.tv_nsec < lstatBuf.st_mtim.tv_nsec))
        return fd;
    }

    OpenRequest request;
    OpenResponse reply;
    ClientContext context;

    request.set_path(path);
    request.set_flags(flags);
    Status status = stub_->Open(&context, request, &reply);
    cout<< request.path() << "calling server with this" << endl;
    if (status.ok() && reply.status() == 1)
    {
      //Save the data in a new file and return file descriptor
      int fd1 = open(absoluteCachePath.c_str(), O_RDWR| O_CREAT , 0666);
      if (fd1 == -1)
      {
        return -errno;
      }
      int ret = pwrite(fd1, reply.filedata().c_str(), reply.filedata().size(), 0);
      if (ret == -1)
      {
        ret = -errno;
      }
      fsync(fd1);
      return fd1;
    }
    else
    {
      cout << status.error_code() << ": " << status.error_message() << std::endl;
      return -1;
    }
  }
};

AfsClientSingleton *AfsClientSingleton ::instancePtr = NULL;

extern "C" int afsGetAttr(const char *path, struct stat *buf)
{
  AfsClientSingleton *afsClient = AfsClientSingleton::getInstance(std::string("localhost:50051"));
  return afsClient->GetAttr(string(path), buf);
}

extern "C" int afsOpen(const char *path, int flags)
{
  AfsClientSingleton *afsClient = AfsClientSingleton::getInstance(std::string("localhost:50051"));
  return afsClient->Open(string(path), flags);
}