// FBuildCoordinatorOptions
//------------------------------------------------------------------------------
#pragma once

// Includes
//------------------------------------------------------------------------------

// Forward Declaration
//------------------------------------------------------------------------------
class AString;

// FBuildCoordinatorOptions
//------------------------------------------------------------------------------
class FBuildCoordinatorOptions
{
public:
    FBuildCoordinatorOptions();

    bool ProcessCommandLine( const AString & commandLine );

private:
    void ShowUsageError();
};

//------------------------------------------------------------------------------
