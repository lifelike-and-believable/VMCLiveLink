// Copyright (c) 2025-2026 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VRMSpringBonesTypes.h"

namespace VRM
{
    /**
     * Validation and diagnostic utilities for VRM spring bone configurations
     */
    
    VRMINTERCHANGE_API struct FVRMValidationResult
    {
        /** False once any error is added (and when Spec is None). */
        bool bIsValid = false;
        /** Problems that don't stop the springs from running (the import logs them as warnings). */
        TArray<FString> Warnings;
        /** Problems that do (bad indices, missing data). */
        TArray<FString> Errors;
        /** Notes, such as counts. */
        TArray<FString> Info;
        
        /** Adds a warning. */
        void AddWarning(const FString& Message) { Warnings.Add(Message); }
        /** Adds an error and marks the result invalid. */
        void AddError(const FString& Message) { Errors.Add(Message); bIsValid = false; }
        /** Adds a note. */
        void AddInfo(const FString& Message) { Info.Add(Message); }
        
        /** Whether there are any warnings or errors. */
        bool HasIssues() const { return Warnings.Num() > 0 || Errors.Num() > 0; }
        
        /** One line: valid or not, with the error and warning counts. */
        FString GetSummary() const
        {
            FString Summary = FString::Printf(TEXT("VRM Validation: %s"), bIsValid ? TEXT("VALID") : TEXT("INVALID"));
            if (Errors.Num() > 0) Summary += FString::Printf(TEXT(" (%d errors)"), Errors.Num());
            if (Warnings.Num() > 0) Summary += FString::Printf(TEXT(" (%d warnings)"), Warnings.Num());
            return Summary;
        }
    };
    
    /**
     * Validate a VRM spring bone configuration for common issues
     */
    VRMINTERCHANGE_API FVRMValidationResult ValidateSpringConfig(const FVRMSpringConfig& Config);
    
    /**
     * Generate a diagnostic report for a VRM spring bone configuration
     */
    VRMINTERCHANGE_API FString GenerateDiagnosticReport(const FVRMSpringConfig& Config);
}