#!/usr/bin/env python3
"""Create a resource and assignment in the test project for verification."""

import sys
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

try:
    import win32com.client
except ImportError:
    print("ERROR: pywin32 required"); sys.exit(1)

def main():
    app = win32com.client.Dispatch("MSProject.Application")
    proj = app.ActiveProject

    print(f"Project: {proj.Name}")

    # Check existing resources
    print(f"\nResources: {proj.Resources.Count}")
    for i in range(1, proj.Resources.Count + 1):
        r = proj.Resources.Item(i)
        if r:
            print(f"  R{r.ID}: '{r.Name}' Type={r.Type} MaxUnits={r.MaxUnits}")

    # Check existing tasks
    print(f"\nTasks: {proj.Tasks.Count}")
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if t:
            print(f"  T{t.ID}: '{t.Name}' Assignments={t.Assignments.Count}")

    # Add a resource if none exist
    if proj.Resources.Count == 0:
        print("\nAdding resource 'TestRes'...")
        r = proj.Resources.Add("TestRes")
        r.StandardRate = "$100/h"
        print(f"  Created: R{r.ID} '{r.Name}' StandardRate={r.StandardRate}")

    # Add assignment to Task 1 if it has none
    t1 = proj.Tasks.Item(1)
    if t1 and t1.Assignments.Count == 0:
        print(f"\nAssigning 'TestRes' to '{t1.Name}'...")
        a = t1.Assignments.Add(t1.ID, 1)  # TaskID, ResourceID
        print(f"  Created assignment: TaskUID={a.TaskUniqueID}, ResourceUID={a.ResourceUniqueID}")
        print(f"  Units={a.Units}, Work={a.Work}, Cost={a.Cost}")

    # Verify
    print("\n=== Final state ===")
    for i in range(1, proj.Tasks.Count + 1):
        t = proj.Tasks.Item(i)
        if not t:
            continue
        for j in range(1, t.Assignments.Count + 1):
            a = t.Assignments.Item(j)
            print(f"  T{t.ID}→R{a.ResourceID}: Units={a.Units}, "
                  f"Work={a.Work}min, Cost=${a.Cost}")

    print("\nDone.")


if __name__ == '__main__':
    main()
