cmake_minimum_required(VERSION 3.16)
project(QtGitStudio VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test Svg Network)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

enable_testing()

# Library target (for testability)
add_library(QtGitStudio_lib STATIC
    src/ActionButtonFeedback.cpp
    src/ActionButtonFeedback.h
    src/BranchTreeWidget.cpp
    src/BranchTreeWidget.h
    src/BranchSwitchErrorDialog.cpp
    src/BranchSwitchErrorDialog.h
    src/CreateBranchDialog.cpp
    src/CreateBranchDialog.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/WelcomeWidget.cpp
    src/WelcomeWidget.h
    src/ProjectController.cpp
    src/ProjectController.h
    src/CloneDialog.cpp
    src/CloneDialog.h
    src/GitBranchService.cpp
    src/GitBranchService.h
    src/GitCommitService.cpp
    src/GitCommitService.h
    src/GitConflictModel.cpp
    src/GitConflictModel.h
    src/GitConflictParser.cpp
    src/GitConflictParser.h
    src/GitConflictService.cpp
    src/GitConflictService.h
    src/GitConflictResolutionService.cpp
    src/GitConflictResolutionService.h
    src/ConflictMarkerParser.cpp
    src/ConflictMarkerParser.h
    src/ConflictResolutionPlan.cpp
    src/ConflictResolutionPlan.h
    src/ConflictResolverWidget.cpp
    src/ConflictResolverWidget.h
    src/GitSnapshot.h
    src/GitError.h
    src/GitStatusParser.h
    src/GitStatusParser.cpp
    src/GitDiffParser.h
    src/GitDiffParser.cpp
    src/EncodingDiagnostic.cpp
    src/DiffViewerWidget.cpp
    src/GitRemoteService.cpp
    src/GitMergeService.cpp
    src/GitCommitGraphService.cpp
    src/GitCommitDiffService.cpp
    src/GitTagService.cpp
    src/TagManagerDialog.cpp
    src/GitStashService.cpp
    src/StashWidget.cpp
    src/GitCherryPickService.cpp
    src/GitRebaseService.cpp
    src/GitBisectService.cpp
    src/BisectWidget.cpp
    src/CommitGraphWidget.cpp
    src/GitEnvironmentService.cpp
    src/GitEnvironmentDialog.cpp
    src/GitHubAccountService.cpp
    src/GitHubAccountDialog.cpp
    src/GitHubRepositoryService.cpp
    src/GitHubRepositoryWidget.cpp
    src/GitHubReleaseService.cpp
    src/ReleaseWidget.cpp
    src/GitHubReleaseAssetService.cpp
    src/ReleaseNotesService.cpp
    src/NotificationManager.cpp
    src/UserFacingError.cpp
    src/LearningWidget.cpp
    src/CommitHistoryWidget.cpp
    src/CommitHistoryWidget.h
    src/DiffPreviewDialog.cpp
    src/DiffPreviewDialog.h
    src/BranchRelationWidget.cpp
    src/BranchRelationWidget.h
    src/CredentialStoreWin.cpp
    src/CredentialStoreWin.h
    src/BranchGraphViewerDialog.cpp
    src/BranchGraphViewerDialog.h
    src/RepositoryResolver.cpp
    src/DropOpenSession.cpp
    src/GitBranchParser.h
    src/GitBranchParser.cpp
    src/GitCommandExecutor.h
    src/GitCommandExecutor.cpp
    src/GitRepository.h
    src/GitRepository.cpp
    src/GitSnapshotService.h
    src/GitSnapshotService.cpp
    src/GitViewModel.h
    src/GitViewModel.cpp
    src/GitController.h
    src/GitController.cpp
)
target_include_directories(QtGitStudio_lib PUBLIC src)
target_link_libraries(QtGitStudio_lib PUBLIC Qt6::Widgets Qt6::Network)
# v4-P6-18 P1-1: Windows Credential Manager (CredWriteW/CredReadW).
if (WIN32)
    target_link_libraries(QtGitStudio_lib PUBLIC advapi32)
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
