//-----------------------------------------------------------------------------
// Once we've written our constraint equations in the symbolic algebra system,
// these routines linearize them, and solve by a modified Newton's method.
// This also contains the routines to detect non-convergence or inconsistency,
// and report diagnostics to the user.
//
// Copyright 2008-2013 Jonathan Westhues.
//-----------------------------------------------------------------------------
#include "solvespace.h"

// This tolerance is used to determine whether two (linearized) constraints
// are linearly dependent. If this is too small, then we will attempt to
// solve truly inconsistent systems and fail. But if it's too large, then
// we will give up on legitimate systems like a skinny right angle triangle by
// its hypotenuse and long side.
const double System::RANK_MAG_TOLERANCE = 1e-4;

// The solver will converge all unknowns to within this tolerance. This must
// always be much less than LENGTH_EPS, and in practice should be much less.
const double System::CONVERGE_TOLERANCE = (LENGTH_EPS/(1e2));

double wrEqTime = 0.0;
double wrJacTime = 0.0;
double evJacTime = 0.0;
double sbsTime = 0.0;
double lsysTime = 0.0;
double crTime = 0.0;

void System::WriteJacobian(int tag) {
    double start = GetSeconds();
    // Clear all
    mat.param.clear();
    mat.eq.clear();
    mat.B.sym.clear();

    for(Param &p : param) {
        if(p.tag != tag) continue;
        mat.param.push_back(p.h);
    }
    mat.n = mat.param.size();

    for(Equation &e : eq) {
        if(e.tag != tag) continue;
        mat.eq.push_back(&e);
    }
    mat.m = mat.eq.size();
    delete mat.A.sym;
    mat.A.sym = new Eigen::SparseMatrix<Expr *>(mat.m, mat.n);

    // Fill the param id to column index map
    std::map<uint32_t, int> paramToIndex;
    for(int j = 0; j < mat.n; j++) {
        paramToIndex[mat.param[j].v] = j;
    }

    for(size_t i = 0; i < mat.eq.size(); i++) {
        Equation *e = mat.eq[i];
        Expr *f = e->e->DeepCopyWithParamsAsPointers(&param, &(SK.param));
        f = f->FoldConstants();

        std::vector<uint32_t> paramsUsed;
        f->ParamsUsedList(&paramsUsed);
        for(uint32_t pv : paramsUsed) {
            if(paramToIndex.find(pv) == paramToIndex.end()) continue;
            Expr *pd = f->PartialWrt(hParam{pv});
            pd = pd->FoldConstants();
            if(pd->IsZeroConst()) continue;
            pd = pd->DeepCopyWithParamsAsPointers(&param, &(SK.param));
            mat.A.sym->insert(i, paramToIndex[pv]) = pd;
        }
        mat.B.sym.push_back(f);
    }
    int nz = mat.A.sym->nonZeros();
    int total = mat.n * mat.m;
    dbp("Equations: %d, Unknows: %d", mat.m, mat.n);
    dbp("Jacobian non zeroes: %d/%d %.5g%%", nz, total, 100.0 * (double)nz / (double)total);
    wrJacTime += GetSeconds() - start;
}

void System::EvalJacobian() {
    double start = GetSeconds();
    using namespace Eigen;
    delete mat.A.num;
    mat.A.num = new Eigen::SparseMatrix<double>(mat.m, mat.n);
    int size = mat.A.sym->outerSize();

    for(int k = 0; k < size; k++) {
        for(SparseMatrix <Expr *>::InnerIterator it(*mat.A.sym, k); it; ++it) {
            double value = it.value()->Eval();
            if(EXACT(value == 0.0)) continue;
            mat.A.num->insert(it.row(), it.col()) = value;
        }
    }
    mat.A.num->makeCompressed();
    evJacTime += GetSeconds() - start;
}

bool System::IsDragged(hParam p) {
    hParam *pp;
    for(pp = dragged.First(); pp; pp = dragged.NextAfter(pp)) {
        if(p.v == pp->v) return true;
    }
    return false;
}

Param *System::GetLastParamSubstitution(Param *p) {
    Param *current = p;
    while(current->substd != NULL) {
        current = current->substd;
        if(current == p) {
            // Break the loop
            current->substd = NULL;
            break;
        }
    }
    return current;
}

void System::SortSubstitutionByDragged(Param *p) {
    //if(p->substd == NULL || p->substd->substd == NULL) return;
    std::vector<Param *> subsParams;
    Param *by = NULL;
    Param *current = p;
    while(current != NULL) {
        subsParams.push_back(current);
        if(IsDragged(current->h)) {
            by = current;
        }
        current = current->substd;
    }
    if(by == NULL) by = p;
    for(Param *p : subsParams) {
       if(p == by) continue;
       p->substd = by;
       p->tag = VAR_SUBSTITUTED;
    }
    by->substd = NULL;
    by->tag = 0;
}

void System::SubstituteParamsByLast(Expr *e) {
    ssassert(e->op != Expr::Op::PARAM_PTR, "Expected an expression that refer to params via handles");

    if(e->op == Expr::Op::PARAM) {
        Param *p = param.FindByIdNoOops(e->parh);
        if(p != NULL) {
            Param *s = GetLastParamSubstitution(p);
            if(s != NULL) {
                e->parh = s->h;
            }
        }
    } else {
        int c = e->Children();
        if(c >= 1) SubstituteParamsByLast(e->a);
        if(c >= 2) SubstituteParamsByLast(e->b);
    }
}

void System::SolveBySubstitution() {
    double start = GetSeconds();
    int i;
    for(i = 0; i < eq.n; i++) {
        Equation *teq = &(eq.elem[i]);
        Expr *tex = teq->e;

        if(tex->op    == Expr::Op::MINUS &&
           tex->a->op == Expr::Op::PARAM &&
           tex->b->op == Expr::Op::PARAM)
        {
            hParam a = tex->a->parh;
            hParam b = tex->b->parh;
            if(!(param.FindByIdNoOops(a) && param.FindByIdNoOops(b))) {
                // Don't substitute unless they're both solver params;
                // otherwise it's an equation that can be solved immediately,
                // or an error to flag later.
                continue;
            }

            if(a.v == b.v) {
                teq->tag = EQ_SUBSTITUTED;
                continue;
            }

            bool isDragged = IsDragged(a);
            if(isDragged) {
                // A is being dragged, so A should stay, and B should go
                hParam t = a;
                a = b;
                b = t;
            }

            Param *pa = param.FindById(a);
            Param *pb = param.FindById(b);

            // Take the last substitution of parameter a
            // The result of this is creating substitution chains
            Param *last = GetLastParamSubstitution(pa);
            last->substd = pb;
            last->tag = VAR_SUBSTITUTED;

            if(pb->substd != NULL) {
                // Break the loops
                GetLastParamSubstitution(pb);
                // if b loop was broken
                if(pb->substd == NULL) {
                    // Clear substitution
                    pb->tag = 0;
                }
            }
            teq->tag = EQ_SUBSTITUTED;
        }
    }

    //
    for(Param &p : param) {
        SortSubstitutionByDragged(&p);
    }

    // Substitute all the equations
    for(auto &req : eq) {
        SubstituteParamsByLast(req.e);
    }

    // Substitute all the parameters with last substitutions
    for(auto &p : param) {
        if(p.substd == NULL) continue;
        p.substd = GetLastParamSubstitution(p.substd);
    }

    sbsTime += GetSeconds() - start;
}

//-----------------------------------------------------------------------------
// Calculate the rank of the Jacobian matrix, by Gram-Schimdt orthogonalization
// in place. A row (~equation) is considered to be all zeros if its magnitude
// is less than the tolerance RANK_MAG_TOLERANCE.
//-----------------------------------------------------------------------------
int System::CalculateRank() {
    using namespace Eigen;
    if(mat.n == 0 || mat.m == 0) return 0;
    double start = GetSeconds();
    SparseQR <SparseMatrix<double>, COLAMDOrdering<int>> solver;
    solver.compute(*mat.A.num);
    int result = solver.rank();
    crTime += GetSeconds() - start;
    return result;
}

bool System::TestRank() {
    EvalJacobian();
    return CalculateRank() == mat.m;
}

bool System::SolveLinearSystem(const Eigen::SparseMatrix <double> &A,
                               const Eigen::VectorXd &B, Eigen::VectorXd *X)
{
    if(A.outerSize() == 0) return true;
    double start = GetSeconds();
    using namespace Eigen;
    SparseQR <SparseMatrix<double>, COLAMDOrdering<int>> solver;
    solver.compute(A);
    *X = solver.solve(B);
    lsysTime += GetSeconds() - start;
    return (solver.info() == Success);
}

bool System::SolveLeastSquares() {
    using namespace Eigen;
    // Scale the columns; this scale weights the parameters for the least
    // squares solve, so that we can encourage the solver to make bigger
    // changes in some parameters, and smaller in others.
    mat.scale = VectorXd(mat.n);
    for(int c = 0; c < mat.n; c++) {
        if(IsDragged(mat.param[c])) {
            // It's least squares, so this parameter doesn't need to be all
            // that big to get a large effect.
            mat.scale[c] = 1 / 20.0;
        } else {
            mat.scale[c]  = 1.0;
        }
    }

    int size = mat.A.sym->outerSize();
    for(int k = 0; k < size; k++) {
        for(SparseMatrix<double>::InnerIterator it(*mat.A.num, k); it; ++it) {
            it.valueRef() *= mat.scale[it.col()];
        }
    }

    SparseMatrix <double> AAt = *mat.A.num * mat.A.num->transpose();
    AAt.makeCompressed();
    int total = AAt.rows() * AAt.cols();
    int nz = AAt.nonZeros();
    dbp("A*A^T non zeroes: %d/%d %.5g%%", nz, total, 100.0 * (double)nz / (double)total);

    VectorXd z(mat.n);

    if(!SolveLinearSystem(AAt, mat.B.num, &z)) return false;

    mat.X = mat.A.num->transpose() * z;

    for(int c = 0; c < mat.n; c++) {
        mat.X[c] *= mat.scale[c];
    }
    return true;
}

bool System::NewtonSolve(int tag) {

    int iter = 0;
    bool converged = false;
    int i;

    // Evaluate the functions at our operating point.
    mat.B.num = Eigen::VectorXd(mat.m);
    for(i = 0; i < mat.m; i++) {
        mat.B.num[i] = (mat.B.sym[i])->Eval();
    }
    do {
        // And evaluate the Jacobian at our initial operating point.
        EvalJacobian();

        if(!SolveLeastSquares()) break;

        // Take the Newton step;
        //      J(x_n) (x_{n+1} - x_n) = 0 - F(x_n)
        for(i = 0; i < mat.n; i++) {
            Param *p = param.FindById(mat.param[i]);
            p->val -= mat.X[i];
            if(isnan(p->val)) {
                // Very bad, and clearly not convergent
                return false;
            }
        }

        // Re-evalute the functions, since the params have just changed.
        for(i = 0; i < mat.m; i++) {
            mat.B.num[i] = (mat.B.sym[i])->Eval();
        }
        // Check for convergence
        converged = true;
        for(i = 0; i < mat.m; i++) {
            if(isnan(mat.B.num[i])) {
                return false;
            }
            if(ffabs(mat.B.num[i]) > CONVERGE_TOLERANCE) {
                converged = false;
                break;
            }
        }
    } while(iter++ < 50 && !converged);
    dbp("Newton Steps: %d", iter);

    return converged;
}

void System::WriteEquationsExceptFor(hConstraint hc, Group *g) {
    double start = GetSeconds();
    int i;
    // Generate all the equations from constraints in this group
    for(i = 0; i < SK.constraint.n; i++) {
        ConstraintBase *c = &(SK.constraint.elem[i]);
        if(c->group.v != g->h.v) continue;
        if(c->h.v == hc.v) continue;

        if(c->HasLabel() && c->type != Constraint::Type::COMMENT &&
                g->allDimsReference)
        {
            // When all dimensions are reference, we adjust them to display
            // the correct value, and then don't generate any equations.
            c->ModifyToSatisfy();
            continue;
        }
        if(g->relaxConstraints && c->type != Constraint::Type::POINTS_COINCIDENT) {
            // When the constraints are relaxed, we keep only the point-
            // coincident constraints, and the constraints generated by
            // the entities and groups.
            continue;
        }

        c->GenerateEquations(&eq);
    }
    // And the equations from entities
    for(i = 0; i < SK.entity.n; i++) {
        EntityBase *e = &(SK.entity.elem[i]);
        if(e->group.v != g->h.v) continue;

        e->GenerateEquations(&eq);
    }
    // And from the groups themselves
    g->GenerateEquations(&eq);
    wrEqTime += GetSeconds() - start;
}

void System::FindWhichToRemoveToFixJacobian(Group *g, List<hConstraint> *bad, bool forceDofCheck) {
    int a, i;

    for(a = 0; a < 2; a++) {
        for(i = 0; i < SK.constraint.n; i++) {
            ConstraintBase *c = &(SK.constraint.elem[i]);
            if(c->group.v != g->h.v) continue;
            if((c->type == Constraint::Type::POINTS_COINCIDENT && a == 0) ||
               (c->type != Constraint::Type::POINTS_COINCIDENT && a == 1))
            {
                // Do the constraints in two passes: first everything but
                // the point-coincident constraints, then only those
                // constraints (so they appear last in the list).
                continue;
            }

            param.ClearTags();
            eq.Clear();
            WriteEquationsExceptFor(c->h, g);
            eq.ClearTags();

            // It's a major speedup to solve the easy ones by substitution here,
            // and that doesn't break anything.
            if(!forceDofCheck) {
                SolveBySubstitution();
            }

            WriteJacobian(0);
            EvalJacobian();

            int rank = CalculateRank();
            if(rank == mat.m) {
                // We fixed it by removing this constraint
                bad->Add(&(c->h));
            }
        }
    }
}

SolveResult System::Solve(Group *g, int *dof, List<hConstraint> *bad,
                          bool andFindBad, bool andFindFree, bool forceDofCheck)
{
    wrEqTime = 0.0;
    wrJacTime = 0.0;
    evJacTime = 0.0;
    sbsTime = 0.0;
    lsysTime = 0.0;
    crTime = 0.0;

    WriteEquationsExceptFor(Constraint::NO_CONSTRAINT, g);

    int i;
    bool rankOk;

/*
    dbp("%d equations", eq.n);
    for(i = 0; i < eq.n; i++) {
        dbp("  %.3f = %s = 0", eq.elem[i].e->Eval(), eq.elem[i].e->Print());
    }
    dbp("%d parameters", param.n);
    for(i = 0; i < param.n; i++) {
        dbp("   param %08x at %.3f", param.elem[i].h.v, param.elem[i].val);
    } */

    // All params and equations are assigned to group zero.
    param.ClearTags();
    eq.ClearTags();

    if(g->allowRedundant || !forceDofCheck) {
        SolveBySubstitution();
    }

    // Before solving the big system, see if we can find any equations that
    // are soluble alone. This can be a huge speedup. We don't know whether
    // the system is consistent yet, but if it isn't then we'll catch that
    // later.
    int alone = 1;
    for(i = 0; i < eq.n; i++) {
        Equation *e = &(eq.elem[i]);
        if(e->tag != 0) continue;

        hParam hp = e->e->ReferencedParams(&param);
        if(hp.v == Expr::NO_PARAMS.v) continue;
        if(hp.v == Expr::MULTIPLE_PARAMS.v) continue;

        Param *p = param.FindById(hp);
        if(p->tag != 0) continue; // let rank test catch inconsistency

        e->tag = alone;
        p->tag = alone;
        WriteJacobian(alone);
        if(!NewtonSolve(alone)) {
            // We don't do the rank test, so let's arbitrarily return
            // the DIDNT_CONVERGE result here.
            rankOk = true;
            // Failed to converge, bail out early
            goto didnt_converge;
        }
        alone++;
    }

    // Now write the Jacobian for what's left, and do a rank test; that
    // tells us if the system is inconsistently constrained.
    WriteJacobian(0);

    rankOk = (!g->allowRedundant) ? TestRank() : false;

    // And do the leftovers as one big system
    if(!NewtonSolve(0)) {
        goto didnt_converge;
    }

    rankOk = (!g->allowRedundant) ? TestRank() : false;

    if(!rankOk) {
        if(!g->allowRedundant) {
            if(andFindBad) FindWhichToRemoveToFixJacobian(g, bad, forceDofCheck);
        }
        if(dof) *dof = -1;
    } else {
        // This is not the full Jacobian, but any substitutions or single-eq
        // solves removed one equation and one unknown, therefore no effect
        // on the number of DOF.
        if(dof) *dof = CalculateDof();
        MarkParamsFree(andFindFree);
    }
    // System solved correctly, so write the new values back in to the
    // main parameter table.
    for(i = 0; i < param.n; i++) {
        Param *p = &(param.elem[i]);
        double val;
        if(p->tag == VAR_SUBSTITUTED) {
            val = p->substd->val;
        } else {
            val = p->val;
        }
        Param *pp = SK.GetParam(p->h);
        pp->val = val;
        pp->known = true;
        pp->free = p->free;
    }

    dbp("WriteEquations: %5.3f ms", wrEqTime  * 1000.0);
    dbp("WriteJacobian:  %5.3f ms", wrJacTime * 1000.0);
    dbp("EvalJacobian:   %5.3f ms", evJacTime * 1000.0);
    dbp("Substitution:   %5.3f ms", sbsTime   * 1000.0);
    dbp("LinearSystem:   %5.3f ms", lsysTime  * 1000.0);
    dbp("CalculateRank:  %5.3f ms", crTime    * 1000.0);

    return rankOk ? SolveResult::OKAY : SolveResult::REDUNDANT_OKAY;

didnt_converge:
    SK.constraint.ClearTags();
    for(i = 0; i < mat.eq.size(); i++) {
        if(ffabs(mat.B.num[i]) > CONVERGE_TOLERANCE || isnan(mat.B.num[i])) {
            // This constraint is unsatisfied.
            if(!mat.eq[i]->h.isFromConstraint()) continue;

            hConstraint hc = mat.eq[i]->h.constraint();
            ConstraintBase *c = SK.constraint.FindByIdNoOops(hc);
            if(!c) continue;
            // Don't double-show constraints that generated multiple
            // unsatisfied equations
            if(!c->tag) {
                bad->Add(&(c->h));
                c->tag = 1;
            }
        }
    }

    return rankOk ? SolveResult::DIDNT_CONVERGE : SolveResult::REDUNDANT_DIDNT_CONVERGE;
}

SolveResult System::SolveRank(Group *g, int *dof, List<hConstraint> *bad,
                              bool andFindBad, bool andFindFree, bool forceDofCheck)
{
    WriteEquationsExceptFor(Constraint::NO_CONSTRAINT, g);

    // All params and equations are assigned to group zero.
    param.ClearTags();
    eq.ClearTags();

    if(!forceDofCheck) {
        SolveBySubstitution();
    }

    // Now write the Jacobian, and do a rank test; that
    // tells us if the system is inconsistently constrained.
    WriteJacobian(0);

    bool rankOk = TestRank();
    if(!rankOk) {
        if(!g->allowRedundant) {
            if(andFindBad) FindWhichToRemoveToFixJacobian(g, bad, forceDofCheck);
        }
    } else {
        // This is not the full Jacobian, but any substitutions or single-eq
        // solves removed one equation and one unknown, therefore no effect
        // on the number of DOF.
        if(dof) *dof = CalculateDof();
        MarkParamsFree(andFindFree);
    }
    return rankOk ? SolveResult::OKAY : SolveResult::REDUNDANT_OKAY;
}

void System::Clear() {
    entity.Clear();
    param.Clear();
    eq.Clear();
    dragged.Clear();
    delete mat.A.num;
    mat.A.num = NULL;
    delete mat.A.sym;
    mat.A.sym = NULL;
}

void System::MarkParamsFree(bool find) {
    // If requested, find all the free (unbound) variables. This might be
    // more than the number of degrees of freedom. Don't always do this,
    // because the display would get annoying and it's slow.
    for(int i = 0; i < param.n; i++) {
        Param *p = &(param.elem[i]);
        p->free = false;

        if(find) {
            if(p->tag == 0) {
                p->tag = VAR_DOF_TEST;
                WriteJacobian(0);
                EvalJacobian();
                int rank = CalculateRank();
                if(rank == mat.m) {
                    p->free = true;
                }
                p->tag = 0;
            }
        }
    }
}

int System::CalculateDof() {
    return mat.n - mat.m;
}

