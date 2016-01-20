//-----------------------------------------------------------------------------
// The user-visible undo/redo operation; whenever they change something, we
// record our state and push it on a stack, and we pop the stack when they
// select undo.
//
// Copyright 2008-2013 Jonathan Westhues.
//-----------------------------------------------------------------------------
#include "solvespace.h"

void SolveSpaceUI::UndoRemember(void) {
    unsaved = true;
    PushFromCurrentOnto(&undo);
    UndoClearStack(&redo);
    UndoEnableMenus();
}

void SolveSpaceUI::UndoUndo(void) {
    if(undo.cnt <= 0) return;

    PushFromCurrentOnto(&redo);
    PopOntoCurrentFrom(&undo);
    UndoEnableMenus();
}

void SolveSpaceUI::UndoRedo(void) {
    if(redo.cnt <= 0) return;

    PushFromCurrentOnto(&undo);
    PopOntoCurrentFrom(&redo);
    UndoEnableMenus();
}

void SolveSpaceUI::UndoEnableMenus(void) {
    EnableMenuById(GraphicsWindow::MNU_UNDO, undo.cnt > 0);
    EnableMenuById(GraphicsWindow::MNU_REDO, redo.cnt > 0);
}

void SolveSpaceUI::PushFromCurrentOnto(UndoStack *uk) {
    int i;

    if(uk->cnt == MAX_UNDO) {
        UndoClearState(&(uk->d[uk->write]));
        // And then write in to this one again
    } else {
        (uk->cnt)++;
    }

    UndoState *ut = &(uk->d[uk->write]);
    *ut = {};
    for(i = 0; i < sketch->group.n; i++) {
        Group *src = &(sketch->group.elem[i]);
        Group dest = *src;
        // And then clean up all the stuff that needs to be a deep copy,
        // and zero out all the dynamic stuff that will get regenerated.
        dest.clean = false;
        dest.solved = {};
        dest.polyLoops = {};
        dest.bezierLoops = {};
        dest.bezierOpens = {};
        dest.polyError = {};
        dest.thisMesh = { sketch };
        dest.runningMesh = { sketch };
        dest.thisShell = { sketch };
        dest.runningShell = { sketch };
        dest.displayMesh = { sketch };
        dest.displayEdges = {};

        dest.remap = {};
        src->remap.DeepCopyInto(&(dest.remap));

        dest.impMesh = { sketch };
        dest.impShell = { sketch };
        dest.impEntity = {};
        ut->group.Add(&dest);
    }
    for(i = 0; i < sketch->request.n; i++) {
        ut->request.Add(&(sketch->request.elem[i]));
    }
    for(i = 0; i < sketch->constraint.n; i++) {
        Constraint *src = &(sketch->constraint.elem[i]);
        Constraint dest = *src;
        dest.dogd = {};
        ut->constraint.Add(&dest);
    }
    for(i = 0; i < sketch->param.n; i++) {
        ut->param.Add(&(sketch->param.elem[i]));
    }
    for(i = 0; i < sketch->style.n; i++) {
        ut->style.Add(&(sketch->style.elem[i]));
    }
    ut->activeGroup = SS.GW.activeGroup;

    uk->write = WRAP(uk->write + 1, MAX_UNDO);
}

void SolveSpaceUI::PopOntoCurrentFrom(UndoStack *uk) {
    if(uk->cnt <= 0) oops();
    (uk->cnt)--;
    uk->write = WRAP(uk->write - 1, MAX_UNDO);

    UndoState *ut = &(uk->d[uk->write]);

    // Free everything in the main copy of the program before replacing it
    Group *g;
    for(g = sketch->group.First(); g; g = sketch->group.NextAfter(g)) {
        g->Clear();
    }
    sketch->group.Clear();
    sketch->request.Clear();
    sketch->constraint.Clear();
    sketch->param.Clear();
    sketch->style.Clear();

    // And then do a shallow copy of the state from the undo list
    ut->group.MoveSelfInto(&(sketch->group));
    ut->request.MoveSelfInto(&(sketch->request));
    ut->constraint.MoveSelfInto(&(sketch->constraint));
    ut->param.MoveSelfInto(&(sketch->param));
    ut->style.MoveSelfInto(&(sketch->style));
    SS.GW.activeGroup = ut->activeGroup;

    // No need to free it, since a shallow copy was made above
    *ut = {};

    // And reset the state everywhere else in the program, since the
    // sketch just changed a lot.
    SS.GW.ClearSuper();
    SS.TW.ClearSuper();
    SS.ReloadAllImported();
    SS.GenerateAll(0, INT_MAX);
    SS.ScheduleShowTW();

    // Activate the group that was active before.
    Group *activeGroup = sketch->group.FindById(SS.GW.activeGroup);
    activeGroup->Activate();
}

void SolveSpaceUI::UndoClearStack(UndoStack *uk) {
    while(uk->cnt > 0) {
        uk->write = WRAP(uk->write - 1, MAX_UNDO);
        (uk->cnt)--;
        UndoClearState(&(uk->d[uk->write]));
    }
    *uk = {}; // for good measure
}

void SolveSpaceUI::UndoClearState(UndoState *ut) {
    int i;
    for(i = 0; i < ut->group.n; i++) {
        Group *g = &(ut->group.elem[i]);

        g->remap.Clear();
    }
    ut->group.Clear();
    ut->request.Clear();
    ut->constraint.Clear();
    ut->param.Clear();
    ut->style.Clear();
    *ut = {};
}

