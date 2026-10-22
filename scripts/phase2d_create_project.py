#!/usr/bin/env python3
"""Create a minimal MS Project file via COM automation to trigger scheduling engine init.

Opens MS Project via COM, creates a project with a few tasks, and saves it.
This should populate all the runtime caches.
"""

import win32com.client
import time
import sys

MPP_PATH = r"C:\Users\timot\SFM Staff Dropbox\Tim King\Claude\Projects\MS Project Decompile\analysis\test_project.mpp"


def main():
    print("Creating minimal MS Project file via COM...")

    try:
        app = win32com.client.Dispatch("MSProject.Application")
    except Exception as e:
        print(f"ERROR connecting to MS Project COM: {e}")
        print("Make sure MS Project is running.")
        sys.exit(1)

    print("Connected to MS Project COM interface.")

    # Make visible
    app.Visible = True
    time.sleep(2)

    # Create a new project
    app.FileNew()
    time.sleep(1)

    proj = app.ActiveProject
    print(f"Project created: '{proj.Name}'")

    # Add a few tasks to ensure scheduling engine is exercised
    tasks = proj.Tasks
    t1 = tasks.Add("Task 1")
    t1.Duration = "5d"

    t2 = tasks.Add("Task 2")
    t2.Duration = "3d"

    t3 = tasks.Add("Milestone")
    t3.Duration = "0d"
    t3.Milestone = True

    # Add a resource
    resources = proj.Resources
    r1 = resources.Add("Developer")

    # Assign resource to task (use ResourceName instead of ResourceID)
    try:
        a1 = t1.Assignments.Add(ResourceID=r1.UniqueID)
    except:
        try:
            app.ResourceAssignment(Name="Developer")
        except:
            print("  (Skipping assignment — not critical)")

    # Add some custom field data
    try:
        t1.Text1 = "Custom text"
        t1.Number1 = 42
        t1.Flag1 = True
    except:
        pass

    print(f"Added {proj.Tasks.Count} tasks, {proj.Resources.Count} resources")

    # Calculate / recalculate project
    try:
        app.CalculateProject()
        print("Project calculated.")
    except:
        pass

    time.sleep(2)

    # Save
    try:
        app.FileSaveAs(MPP_PATH)
        print(f"Saved to {MPP_PATH}")
    except Exception as e:
        print(f"Save warning: {e}")
        # Try alternative save
        try:
            proj.SaveAs(MPP_PATH)
        except:
            pass

    print("Done! Project file created and scheduling engine should be initialized.")
    print("Run phase2d_dump_runtime.py now to dump the populated caches.")


if __name__ == '__main__':
    main()
