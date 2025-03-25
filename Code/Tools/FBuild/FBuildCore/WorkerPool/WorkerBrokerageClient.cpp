// WorkerBrokerageClient - Client-side worker discovery
//------------------------------------------------------------------------------

// Includes
//------------------------------------------------------------------------------
#include "WorkerBrokerageClient.h"

// FBuildCore
#include "Tools/FBuild/FBuildCore/FLog.h"

// Core
#include "Core/Env/Env.h"
#include "Core/FileIO/FileIO.h"
#include "Core/FileIO/PathUtils.h"
#include "Core/Network/Network.h"
#include "Core/Profile/Profile.h"

// CONSTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerageClient::WorkerBrokerageClient() = default;

// DESTRUCTOR
//------------------------------------------------------------------------------
WorkerBrokerageClient::~WorkerBrokerageClient() = default;

//------------------------------------------------------------------------------
