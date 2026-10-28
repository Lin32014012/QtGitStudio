cmake_minimum_required(VERSION 3.22)
project(QtGitStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Svg Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

# Library target (for testability)
add_library(QtGitStudio_lib STATIC
    # core
    src/core/DropOpenSession.cpp
    src/core/DropOpenSession.h
    src/core/EncodingDiagnostic.cpp
    src/core/EncodingDiagnostic.h
    src/core/GitCommandExecutor.cpp
    src/core/GitCommandExecutor.h
    src/core/GitController.cpp
    src/core/GitController.h
    src/core/GitError.h
    src/core/GitRepository.cpp
    src/core/GitRepository.h
    src/core/GitSnapshot.h
    src/core/GitSnapshotService.cpp
    src/core/GitSnapshotService.h
    src/core/GitViewModel.cpp
    src/core/GitViewModel.h
    src/core/ProjectController.cpp
    src/core/ProjectController.h
    src/core/RepositoryResolver.cpp
    src/core/RepositoryResolver.h
    src/core/TextEncoding.cpp
    src/core/TextEncoding.h
    src/core/UserFacingError.cpp
    src/core/UserFacingError.h
    # parsers
    src/parsers/ConflictMarkerParser.cpp
    src/parsers/ConflictMarkerParser.h
    src/parsers/GitBranchParser.cpp
    src/parsers/GitBranchParser.h
    src/parsers/GitConflictParser.cpp
    src/parsers/GitConflictParser.h
    src/parsers/GitDiffParser.cpp
    src/parsers/GitDiffParser.h
    src/parsers/GitStatusParser.cpp
    src/parsers/GitStatusParser.h
    # services
    src/services/PracticeService.cpp
    src/services/PracticeService.h
    src/services/ConflictResolutionPlan.cpp
    src/services/ConflictResolutionPlan.h
    src/services/GitBisectService.cpp
    src/services/GitBisectService.h
    src/services/GitBranchService.cpp
    src/services/GitBranchService.h
    src/services/GitCherryPickService.cpp
    src/services/GitCherryPickService.h
    src/services/GitCommitDiffService.cpp
    src/services/GitCommitDiffService.h
    src/services/GitCommitGraphService.cpp
    src/services/GitCommitGraphService.h
    src/services/GitCommitService.cpp
    src/services/GitCommitService.h
    src/services/GitConflictModel.cpp
    src/services/GitConflictModel.h
    src/services/GitConflictResolutionService.cpp
    src/services/GitConflictResolutionService.h
    src/services/GitConflictService.cpp
    src/services/GitConflictService.h
    src/services/GitEnvironmentService.cpp
    src/services/GitEnvironmentService.h
    src/services/GitMergeService.cpp
    src/services/GitMergeService.h
    src/services/GitRebaseService.cpp
    src/services/GitRebaseService.h
    src/services/GitRemoteService.cpp
    src/services/GitRemoteService.h
    src/services/GitStashService.cpp
    src/services/GitStashService.h
    src/services/GitTagService.cpp
    src/services/GitTagService.h
    src/services/ReleaseNotesService.cpp
    src/services/ReleaseNotesService.h
    # github
    src/github/GitHubAccountService.cpp
    src/github/GitHubAccountService.h
    src/github/GitHubReleaseAssetService.cpp
    src/github/GitHubReleaseAssetService.h
    src/github/GitHubReleaseService.cpp
    src/github/GitHubReleaseService.h
    src/github/GitHubRepositoryService.cpp
    src/github/GitHubRepositoryService.h
    # platform
    src/platform/CredentialStore.h
    src/platform/CredentialStoreFactory.cpp
    src/platform/CredentialStoreFactory.h
    # ui
    src/ui/ActionButtonFeedback.cpp
    src/ui/ActionButtonFeedback.h
    src/ui/BisectWidget.cpp
    src/ui/BisectWidget.h
    src/ui/BranchGraphViewerDialog.cpp
    src/ui/BranchGraphViewerDialog.h
    src/ui/BranchRelationWidget.cpp
    src/ui/BranchRelationWidget.h
    src/ui/BranchSwitchErrorDialog.cpp
    src/ui/BranchSwitchErrorDialog.h
    src/ui/BranchTreeWidget.cpp
    src/ui/BranchTreeWidget.h
    src/ui/CloneDialog.cpp
    src/ui/CloneDialog.h
    src/ui/CommitGraphWidget.cpp
    src/ui/CommitGraphWidget.h
    src/ui/CommitHistoryWidget.cpp
    src/ui/CommitHistoryWidget.h
    src/ui/ConflictResolverWidget.cpp
    src/ui/ConflictResolverWidget.h
    src/ui/CreateBranchDialog.cpp
    src/ui/CreateBranchDialog.h
    src/ui/DiffPreviewDialog.cpp
    src/ui/DiffPreviewDialog.h
    src/ui/DiffViewerWidget.cpp
    src/ui/DiffViewerWidget.h
    src/ui/GitEnvironmentDialog.cpp
    src/ui/GitEnvironmentDialog.h
    src/ui/GitHubAccountDialog.cpp
    src/ui/GitHubAccountDialog.h
    src/ui/GitHubRepositoryWidget.cpp
    src/ui/GitHubRepositoryWidget.h
    src/ui/LearningWidget.cpp
    src/ui/LearningWidget.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/NotificationManager.cpp
    src/ui/NotificationManager.h
    src/ui/ReleaseWidget.cpp
    src/ui/ReleaseWidget.h
    src/ui/StashWidget.cpp
    src/ui/StashWidget.h
    src/ui/TagManagerDialog.cpp
    src/ui/TagManagerDialog.h
    src/ui/WelcomeWidget.cpp
    src/ui/WelcomeWidget.h
)
# Sources are grouped by layer; every layer directory is on the include
# path so headers keep their short #include "Name.h" form.
target_include_directories(QtGitStudio_lib PUBLIC
    src/core src/parsers src/services src/github src/platform src/ui)
target_link_libraries(QtGitStudio_lib PUBLIC Qt6::Widgets Qt6::Network Qt6::Svg)
# v4-P6-18 P1-1: Windows Credential Manager (CredWriteW/CredReadW).
# Single source of truth for the version: project(... VERSION x.y.z).
target_compile_definitions(QtGitStudio_lib PUBLIC
    QTGITSTUDIO_VERSION="${PROJECT_VERSION}")

# Credential backend is selected at compile time behind CredentialStore.
if (WIN32)
    target_sources(QtGitStudio_lib PRIVATE
        src/platform/CredentialStoreWin.cpp
        src/platform/CredentialStoreWin.h)
    target_link_libraries(QtGitStudio_lib PUBLIC advapi32)
else()
    # Portable fallback: in-memory only, never written to disk.
    target_sources(QtGitStudio_lib PRIVATE
        src/platform/CredentialStoreSession.cpp
        src/platform/CredentialStoreSession.h)
endif()

# Executable
add_executable(QtGitStudio WIN32
    src/main.cpp
    resources/QtGitStudio.qrc
)
target_link_libraries(QtGitStudio PRIVATE QtGitStudio_lib Qt6::Svg)

if(WIN32)
    target_sources(QtGitStudio PRIVATE resources/app_icon.rc)
endif()

# Tests
add_subdirectory(tests)

if(WIN32)
    get_target_property(QTGITSTUDIO_IS_GUI QtGitStudio WIN32_EXECUTABLE)
    if(NOT QTGITSTUDIO_IS_GUI)
        message(FATAL_ERROR "QtGitStudio must use the Windows GUI subsystem")
    endif()

    foreach(TEST_TARGET IN ITEMS
        test_mainwindow
        test_parsers
        test_services
        test_viewmodel
        test_controller
        test_project_controller
        test_branch_service
        test_commit_service
    )
        get_target_property(TEST_IS_GUI ${TEST_TARGET} WIN32_EXECUTABLE)
        if(TEST_IS_GUI)
            message(FATAL_ERROR "${TEST_TARGET} must keep the Windows console subsystem")
        endif()
    endforeach()
endif()
