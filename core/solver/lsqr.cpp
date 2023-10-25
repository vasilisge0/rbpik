#include <cuda_runtime.h>
#include <cmath>
#include <iostream>
#include "cublas_v2.h"
#include "cuda_fp16.h"
#include "magma_lapack.h"
#include "magma_v2.h"
#include <iomanip>


#include "../../cuda/preconditioner/preconditioner_kernels.cuh"
#include "../../cuda/solver/lsqr_kernels.cuh"
#include "../../utils/io.hpp"
#include "../blas/blas.hpp"
#include "../matrix/dense/dense.hpp"
#include "../matrix/sparse/sparse.hpp"
#include "../matrix/mtxop.hpp"
#include "../memory/memory.hpp"
#include "../preconditioner/preconditioner.hpp"
#include "base_types.hpp"
#include "lsqr.hpp"
#include "solver.hpp"


#define VISUALS 1

namespace rls {
namespace solver {
namespace lsqr {

template struct Workspace<CUDA, double, double, double, double, magma_int_t>;
template struct Workspace<CUDA, double, float, double, double, magma_int_t>;
// template struct Workspace<CUDA, double, __half, double, magma_int_t>;
template struct Workspace<CUDA, double, float, float, double, magma_int_t>;
// template struct Workspace<CUDA, double, __half, __half, magma_int_t>;
template struct Workspace<CUDA, float, float, float, double, magma_int_t>;
template struct Workspace<CUDA, float, float, float, float, magma_int_t>;
// template struct Workspace<CUDA, float, __half, float, magma_int_t>;

//template struct Workspace<CPU, double, double, double, magma_int_t>;
//

template <ContextType device, typename vtype, typename vtype_internal,
          typename vtype_precond_apply, typename vtype_refine, typename itype>
Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::~Workspace()
{
    // Frees memory allocated for mixed precision implementation.
    if (!std::is_same<vtype, vtype_internal>::value) {
    }
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::Workspace(std::shared_ptr<Context<device>> context, dim2 size)
{
    u    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[0], 1)));
    v    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));
    w    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));
    temp = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[0], 1)));
    temp1 = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));
    // Allocate memory required for mixed precision implementation.
    if (!std::is_same<vtype, vtype_internal>::value) {
        u_in    = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], 1)));
        v_in    = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[1], 1)));
        temp_in = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], 1)));
        mtx_in  = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], size[1])));
    }

}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::Workspace(
    std::shared_ptr<Context<device>> context,
    std::shared_ptr<MtxOp<device>> mtx,
    std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
    std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs)
{
    auto size = mtx->get_size();
    u    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[0], 1)));
    v    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));
    w    = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));
    temp = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[0], 1)));
    temp1 = rls::share(matrix::Dense<device, vtype>::create(context, dim2(size[1], 1)));

    if (!std::is_same<vtype, vtype_refine>::value) {
        if (auto t = dynamic_cast<matrix::Sparse<device, vtype_refine, itype>*>(mtx.get()); t != nullptr)
        {
            mtx_ = matrix::Sparse<device, vtype, itype>::create(context, t->get_size(), t->get_nnz());
            auto spmtx = static_cast<matrix::Sparse<device, vtype, itype>*>(mtx_.get());
            spmtx->copy_from(t);
        }
        else if (auto t = dynamic_cast<matrix::Dense<device, vtype_refine>*>(mtx.get()); t != nullptr)
        {
            mtx_ = matrix::Dense<device, vtype>::create(context, t->get_size());
            auto dnmtx = static_cast<matrix::Dense<device, vtype>*>(mtx_.get());
            dnmtx->copy_from(t);
        }
        sol_ = matrix::Dense<device, vtype>::create(context, sol->get_size());
        sol_->copy_from(sol.get());
    }
    else {
        mtx_ = mtx;
        sol_ = std::dynamic_pointer_cast<matrix::Dense<device, vtype>>(sol);
    }

    // Allocate memory required for mixed precision implementation.
    if (!std::is_same<vtype, vtype_internal>::value) {
        u_in    = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], 1)));
        v_in    = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[1], 1)));
        temp_in = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], 1)));
        if (auto t = dynamic_cast<matrix::Sparse<device, vtype_refine, itype>*>(mtx.get()); t != nullptr)
        {
            mtx_apply = matrix::Sparse<device, vtype_internal, itype>::create(context, t->get_size(), t->get_nnz());
            auto spmtx = static_cast<matrix::Sparse<device, vtype_internal, itype>*>(mtx_apply.get());
            spmtx->copy_from(t);
        }
        else if (auto t = dynamic_cast<matrix::Dense<device, vtype_refine>*>(mtx.get()); t != nullptr)
        {
            mtx_in  = rls::share(matrix::Dense<device, vtype_internal>::create(context, dim2(size[0], size[1])));
            mtx_apply = matrix::Dense<device, vtype_internal>::create(context, t->get_size());
            auto dnmtx = static_cast<matrix::Dense<device, vtype_internal>*>(mtx_apply.get());
            dnmtx->copy_from(t);
        }
    }

    res_ = rls::share(matrix::Dense<device, vtype>::create(context, rhs->get_size()));
    res_->copy_from(rhs.get());
    rhs_refine_ = rls::share(matrix::Dense<device, vtype_refine>::create(context, rhs->get_size()));
    rhs_refine_->copy_from(rhs.get());

    if (!std::is_same<vtype, vtype_precond_apply>::value) {
        v_apply_ = rls::share(matrix::Dense<device, vtype_precond_apply>::create(context, sol->get_size()));
    }
    temp_refine_ = rls::share(matrix::Dense<device, vtype_refine>::create(context, sol->get_size()));
    temp_refine_->zeros();
}


template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::Workspace(
            std::shared_ptr<Context<device>> context, std::shared_ptr<MtxOp<device>> mtx,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> true_sol,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> true_error) : Workspace(context, mtx, sol, rhs)
{
    true_sol_ = true_sol;
    true_error_ = true_error;
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::Workspace(
            std::shared_ptr<Context<device>> context, std::shared_ptr<MtxOp<device>> mtx,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> true_sol,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> true_error,
          std::shared_ptr<matrix::Dense<device, vtype_refine>> noisy_sol) : Workspace(context, mtx, sol, rhs)
{
    true_sol_ = true_sol;
    true_error_ = true_error;
    noisy_sol_ = noisy_sol;
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>
    Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(std::shared_ptr<Context<device>> context, dim2 size)
{
    return std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>(new Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>(context, size));
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>
   Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(std::shared_ptr<Context<device>> context,
            std::shared_ptr<MtxOp<device>> mtx,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs)
{
    return std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>(
        new Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>(context, mtx, sol, rhs));
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>
   Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(std::shared_ptr<Context<device>> context,
            std::shared_ptr<MtxOp<device>> mtx,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> true_sol,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> true_error)
{
    return std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>(
        new Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>(context, mtx, sol, rhs, true_sol, true_error));
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>
   Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(std::shared_ptr<Context<device>> context,
            std::shared_ptr<MtxOp<device>> mtx,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> true_sol,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> true_error,
            std::shared_ptr<matrix::Dense<device, vtype_refine>> noisy_sol)
{
    return std::shared_ptr<Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>>(
        new Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>(context, mtx, sol, rhs, true_sol, true_error, noisy_sol));
}



}   // end of namespace lsqr


namespace {


template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void initialize(std::shared_ptr<Context<device>> context, dim2 size,
                itype* iter, vtype* mtx, vtype* rhs,
                lsqr::Workspace<device, vtype, vtype_internal,
                                vtype_precond_apply, vtype_refine, itype>* workspace)
{
    *iter = 0;
    itype inc = 1;
    workspace->beta = blas::norm2(context, size[0], rhs, inc);
    blas::copy(context, size[0], rhs, inc, workspace->u->get_values(), inc);
    blas::scale(context, size[0], 1 / workspace->beta, workspace->u->get_values(), inc);
    blas::gemv(context, MagmaTrans, size[0], size[1], 1.0, mtx, size[0],
               workspace->u, inc, 1.0, workspace->v, inc);

    workspace->alpha = blas::norm2(context, size[0], workspace->v, inc);
    blas::scale(context, size[0], 1 / workspace->alpha, workspace->v, inc);
    blas::copy(context, size[0], workspace->v, inc, workspace->w, inc);
    workspace->rho_bar = workspace->alpha;
    workspace->phi_bar = workspace->beta;
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void initialize(std::shared_ptr<Context<device>> context,
                std::shared_ptr<iterative::LsqrConfig<
                    vtype, vtype_internal,
                    vtype_precond_apply, vtype_refine, itype>>
                    config,
                iterative::Logger* logger,
                PrecondOperator<device, vtype_precond_apply,
                                         itype>* precond,
                lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
                matrix::Dense<device, vtype>* mtx_in,
                matrix::Dense<device, vtype>* rhs_in)
{
    // auto precond_mtx = precond->get_mtx();
    auto size = mtx_in->get_size();
    auto mtx = mtx_in->get_values();
    auto rhs = rhs_in->get_values();
    itype num_rows = size[0];
    itype num_cols = size[1];
    vtype one = 1.0;
    vtype zero = 0.0;
    workspace->inc = 1;
    if (!std::is_same<vtype_internal, vtype>::value) {
        workspace->mtx_in->copy_from(mtx_in);
    }
    workspace->beta = blas::norm2(context, num_rows, rhs, workspace->inc);
    blas::copy(context, num_rows, rhs, workspace->inc, workspace->u->get_values(), workspace->inc);
    blas::scale(context, num_rows, one / workspace->beta, workspace->u->get_values(), 1);
    if (!std::is_same<vtype_internal, vtype>::value) {
        auto u_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->u.get(), span(0, 0));
        auto v_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->v.get(), span(0, 0));
        workspace->u_in->copy_from(u_cur.get());
        workspace->v_in->copy_from(v_cur.get());
        blas::gemv(context, MagmaTrans, num_rows, num_cols, 1.0, workspace->mtx_in,
                   num_rows, workspace->u_in, workspace->inc, 0.0,
                   workspace->v_in, workspace->inc);
        v_cur->copy_from(workspace->v_in.get());
    } else {
        blas::gemv(context, MagmaTrans, num_rows, num_cols, one, (vtype*)mtx, num_rows,
                   (vtype*)workspace->u->get_values(), 1, zero, (vtype*)workspace->v->get_values(),
                   1);
    }
    precond->apply(context, MagmaTrans, workspace->v.get());
    workspace->alpha = blas::norm2(context, num_cols, workspace->v->get_values(),
        workspace->inc);
    blas::scale(context, num_cols, one / workspace->alpha, workspace->v->get_values(),
                workspace->inc);
    blas::copy(context, num_cols, workspace->v->get_values(), workspace->inc, workspace->w->get_values(),
               workspace->inc);
    workspace->phi_bar = workspace->beta;
    workspace->rho_bar = workspace->alpha;
}

// Step 1 of preconditioned LSQR.
template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void step_1(std::shared_ptr<Context<device>> context,
            std::shared_ptr<iterative::LsqrConfig<
                vtype, vtype_internal,
                vtype_precond_apply, vtype_refine, itype>>
                config,
            iterative::Logger* logger,
            PrecondOperator<device, vtype_precond_apply,
                                     itype>* precond,
            lsqr::Workspace<device, vtype, vtype_internal,
                            vtype_precond_apply, vtype_refine, itype>* workspace,
            matrix::Dense<device, vtype>* mtx_in)
{
    auto num_rows = mtx_in->get_size()[0];
    auto num_cols = mtx_in->get_size()[1];
    //itype ld_precond = precond->get_size()[0];
    //auto precond_mtx = precond->get_values();
    auto mtx = mtx_in->get_values();
    itype inc = 1;
    double first_measurement = 0.0;
    // Compute new u vector.
    blas::scale(context, num_rows, workspace->alpha, workspace->u->get_values(), inc);
    blas::copy(context, num_cols, workspace->v->get_values(), inc, workspace->temp->get_values(), inc);
    precond->apply(context, MagmaNoTrans, workspace->temp.get());
    vtype one = 1.0;
    vtype zero = 0.0;
    vtype minus_one = -1.0;
    if (!std::is_same<vtype_internal, vtype>::value) {
        auto temp_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->temp, span(0, 0));
        auto u_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->u, span(0, 0));
        workspace->temp_in->copy_from(temp_cur.get());
        workspace->u_in->copy_from(u_cur.get());
        blas::gemv(context, MagmaNoTrans, num_rows, num_cols, 1.0, workspace->mtx_in,
                   num_rows, workspace->temp_in, inc, -1.0, workspace->u_in,
                   inc);
        workspace->u->copy_from(workspace->u_in.get());
    } else {
        blas::gemv(context, MagmaNoTrans, num_rows, num_cols, one, mtx, num_rows,
                   workspace->temp->get_values(), inc, minus_one, workspace->u->get_values(), inc);
    }
    workspace->beta = blas::norm2(context, num_rows, workspace->u->get_values(), inc);
    blas::scale(context, num_rows, one / workspace->beta, workspace->u->get_values(), inc);
    // Compute new v vector.
    if (!std::is_same<vtype_internal, vtype>::value) {
        auto temp_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->temp, span(0, 0));
        auto u_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->u, span(0, 0));
        workspace->temp_in->copy_from(temp_cur.get());
        workspace->u_in->copy_from(u_cur.get());
        blas::gemv(context, MagmaTrans, num_rows, num_cols, 1.0, workspace->mtx_in,
                   num_rows, workspace->u_in, inc, 0.0, workspace->temp_in,
                   inc);
        workspace->temp->copy_from(workspace->temp_in.get());
    } else {
        blas::gemv(context, MagmaTrans, num_rows, num_cols, one, mtx, num_rows,
                   workspace->u->get_values(), inc, zero, workspace->temp->get_values(), inc);
    }
    precond->apply(context, MagmaTrans, workspace->temp.get());
    blas::axpy(context, num_cols, -(workspace->beta), workspace->v->get_values(), 1,
               workspace->temp->get_values(), 1);
    workspace->alpha = blas::norm2(context, num_cols, workspace->temp->get_values(), inc);
    blas::scale(context, num_cols, 1 / workspace->alpha, workspace->temp->get_values(), inc);
    blas::copy(context, num_cols, workspace->temp->get_values(), inc, workspace->v->get_values(), inc);
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void step_2(std::shared_ptr<Context<device>> context,
            std::shared_ptr<iterative::LsqrConfig<
                vtype, vtype_internal,
                vtype_precond_apply, vtype_refine, itype>>
                config,
            iterative::Logger* logger,
            PrecondOperator<device, vtype_precond_apply,
                                     itype>* precond,
            lsqr::Workspace<device, vtype, vtype_internal,
                            vtype_precond_apply, vtype_refine, itype>* workspace,
            matrix::Dense<device, vtype>* mtx_in,
            matrix::Dense<device, vtype>* sol_in)
{
    //double first_measurement = 0.0;
    //if (logger.measure_runtime_) {
    //    first_measurement = magma_sync_wtime(context->get_queue());
    //}

    auto num_rows = mtx_in->get_size()[0];
    auto num_cols = mtx_in->get_size()[1];
    auto mtx = mtx_in->get_values();
    auto sol = sol_in->get_values();
    itype ld_precond = precond->get_mtx()->get_size()[0];
    itype inc = 1;
    vtype one = 1.0;
    auto rho = std::sqrt(((workspace->rho_bar) * (workspace->rho_bar) +
                          workspace->beta * workspace->beta));
    auto c = (workspace->rho_bar) / rho;
    auto s = workspace->beta / rho;
    auto theta = s * workspace->alpha;
    workspace->rho_bar = -c * workspace->alpha;
    auto phi = c * (workspace->phi_bar);
    workspace->phi_bar = s * (workspace->phi_bar);
    blas::copy(context, num_cols, workspace->w->get_values(), inc, workspace->temp->get_values(), inc);
    precond->apply(context, MagmaNoTrans, workspace->temp.get());
    blas::axpy(context, num_cols, phi / rho, workspace->temp->get_values(), 1, sol, 1);
    // Compute new vector w.
    blas::scale(context, num_cols, -(theta / rho), workspace->w->get_values(), inc);
    blas::axpy(context, num_cols, one, workspace->v->get_values(), 1, workspace->w->get_values(), 1);
    //if (logger.measure_runtime_) {
    //    logger.runtime_ = magma_sync_wtime(context->get_queue()) - first_measurement;
    //}
    //
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
bool check_stopping_criteria(std::shared_ptr<Context<device>> context,
                             std::shared_ptr<iterative::LsqrConfig<
                                 vtype, vtype_internal,
                                 vtype_precond_apply, vtype_refine, itype>>
                                 config,
                             iterative::Logger* logger,
                             matrix::Dense<device, vtype>* mtx_in,
                             matrix::Dense<device, vtype>* rhs_in,
                             matrix::Dense<device, vtype>* sol_in,
                             vtype* res_vector)
{
     auto mtx = mtx_in->get_values();
     auto rhs = rhs_in->get_values();
     auto sol = sol_in->get_values();
     auto num_rows = mtx_in->get_size()[0];
     auto num_cols = mtx_in->get_size()[1];
     //workspace->completed_iterations += 1;
     //magma_int_t inc = 1;
     //vtype one = 1.0;
     //vtype minus_one = -1.0;
     //blas::copy(context, num_rows, rhs, inc, res_vector, inc);
     //blas::gemv(context, MagmaNoTrans, num_rows, num_cols, minus_one, mtx, num_rows, sol, inc,
     //           one, res_vector, inc);
     //workspace->resnorm = blas::norm2(context, num_rows, res_vector, inc);
     //workspace->resnorm = workspace->resnorm / workspace->rhsnorm;
     //if ((logger.completed_iterations_ >= config->get_iterations()) || (workspace->resnorm < config->get_tolerance())) {
     //    return true;
     //} else {
         return false;
     //}
}


}  // end of anonymous namespace


template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void run_lsqr(std::shared_ptr<Context<device>> context,
              iterative::LsqrConfig<
                  vtype, vtype_internal,
                  vtype_precond_apply, vtype_refine, itype>*
                  config,
              iterative::Logger* logger,
              PrecondOperator<device, vtype_precond_apply,
                                     itype>* precond,
              lsqr::Workspace<device, vtype, vtype_internal,
                             vtype_precond_apply, vtype_refine, itype>* workspace,
              matrix::Dense<device, vtype_refine>* mtx,
              matrix::Dense<device, vtype_refine>* rhs,
              matrix::Dense<device, vtype_refine>* sol)
{
    //initialize(context, config, logger, precond, workspace, mtx, rhs);
    //while (1) {
    //    step_1(context, config, logger, precond, workspace, mtx);
    //    step_2(context, config, logger, precond, workspace, mtx, sol);
    //    //if (check_stopping_criteria(context, config, logger, workspace->mtx_refine, workspace->rhs_refine, workspace->sol, workspace->temp->get_values())) {
    //    //    break;
    //    //}
    //}
}

template void run_lsqr(std::shared_ptr<Context<CUDA>> context,
              iterative::LsqrConfig<double, double, double, double, magma_int_t>* config,
              iterative::Logger* logger,
              PrecondOperator<CUDA, double, magma_int_t>* precond,
              lsqr::Workspace<CUDA, double, double, double, double, magma_int_t>* workspace,
              matrix::Dense<CUDA, double>* mtx,
              matrix::Dense<CUDA, double>* rhs,
              matrix::Dense<CUDA, double>* sol);

//template
//void run_lsqr(std::shared_ptr<Context<CUDA>> context,
//              iterative::Logger logger,
//              PrecondOperator<CUDA, float, magma_int_t>* precond,
//              lsqr::Workspace<double, float, float, magma_int_t>* workspace,
//              matrix::Dense<CUDA, double>* mtx,
//              matrix::Dense<CUDA, double>* rhs,
//              matrix::Dense<CUDA, double>* sol);
//
//template
//void run_lsqr(std::shared_ptr<Context<CUDA>> context,
//              iterative::Logger logger,
//              PrecondOperator<CUDA, __half, magma_int_t>* precond,
//              lsqr::Workspace<double, __half, __half, magma_int_t>* workspace,
//              matrix::Dense<CUDA, double>* mtx,
//              matrix::Dense<CUDA, double>* rhs,
//              matrix::Dense<CUDA, double>* sol);
//
//template
//void run_lsqr(std::shared_ptr<Context<CUDA>> context,
//              iterative::Logger logger,
//              PrecondOperator<CUDA, float, magma_int_t>* precond,
//              lsqr::Workspace<float, float, float, magma_int_t>* workspace,
//              matrix::Dense<CUDA, float>* mtx,
//              matrix::Dense<CUDA, float>* rhs,
//              matrix::Dense<CUDA, float>* sol);
//
//template
//void run_lsqr(std::shared_ptr<Context<CUDA>> context,
//              iterative::Logger logger,
//              PrecondOperator<CUDA, __half, magma_int_t>* precond,
//              lsqr::Workspace<float, __half, __half, magma_int_t>* workspace,
//              matrix::Dense<CUDA, float>* mtx,
//              matrix::Dense<CUDA, float>* rhs,
//              matrix::Dense<CUDA, float>* sol);
//


template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void initialize(std::shared_ptr<Context<device>> context,
                iterative::LsqrConfig<vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* config,
                iterative::Logger* logger,
                Preconditioner* precond,
                lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
                matrix::Sparse<device, vtype, itype>* mtx_in,
                matrix::Dense<device, vtype>* rhs_in)
{
    auto size = mtx_in->get_size();
    auto rhs = rhs_in->get_values();
    itype num_rows = size[0];
    itype num_cols = size[1];
    vtype one = 1.0;
    vtype zero = 0.0;
    workspace->inc = 1;
    workspace->beta = blas::norm2(context, num_rows, rhs, workspace->inc);
    auto queue = context->get_queue();
    blas::copy(context, num_rows, rhs, 1, workspace->u->get_values(), 1);
    blas::scale(context, num_rows, one / workspace->beta, workspace->u->get_values(), 1);
    auto exec = context->get_executor();
    auto v_cur = matrix::Dense<device, vtype>::create_submatrix(workspace->v.get(), span(0, 0));
    if (!std::is_same<vtype_internal, vtype>::value) {
        workspace->v_in->copy_from(workspace->v.get());
        workspace->u_in->copy_from(workspace->u.get());
        auto t = static_cast<matrix::Sparse<device, vtype_internal, itype>*>(workspace->mtx_apply.get());
        t->transpose()->apply((vtype_internal)one, workspace->u_in.get(), (vtype_internal)zero, workspace->v_in.get());
        v_cur->copy_from(workspace->v_in.get());
    } else {
        // error here, something synchronizes differently each time
        mtx_in->transpose()->apply(one, workspace->u.get(), zero, v_cur.get());
    }
    if (!std::is_same<vtype_precond_apply, vtype>::value) {
        workspace->v_apply_->copy_from(v_cur.get());
        auto precond_operator = static_cast<PrecondOperator<device, vtype_precond_apply, itype>*>(precond);
        precond_operator->apply(context, MagmaTrans, workspace->v_apply_.get());
        v_cur.get()->copy_from(workspace->v_apply_.get());
    }
    else {
        auto precond_operator = static_cast<PrecondOperator<device, vtype, itype>*>(precond);
        // error before there
        precond_operator->apply(context, MagmaTrans, v_cur.get());
    }
    workspace->alpha = blas::norm2(context, num_cols, v_cur->get_values(), 1);
    cudaDeviceSynchronize();
    blas::scale(context, num_cols, one / workspace->alpha, v_cur->get_values(),
                workspace->inc);
    blas::copy(context, num_cols, v_cur->get_values(), workspace->inc, workspace->w->get_values(),
               workspace->inc);
    workspace->phi_bar = workspace->beta;
    workspace->rho_bar = workspace->alpha;
}

// Step 1 of preconditioned LSQR.
template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void step_1(std::shared_ptr<Context<device>> context,
            iterative::LsqrConfig<vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* config,
            iterative::Logger* logger,
            //PrecondOperator<device, vtype_precond_apply, itype>* precond,
            Preconditioner* precond,
            lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
            matrix::Sparse<device, vtype, itype>* mtx_in)
{
    auto num_rows = mtx_in->get_size()[0];
    auto num_cols = mtx_in->get_size()[1];
    auto mtx = mtx_in->get_values();
    itype inc = 1;
    double first_measurement = 0.0;
    // Compute new u vector.
    blas::scale(context, num_rows, workspace->alpha, workspace->u->get_values(), inc);
    blas::copy(context, num_cols, workspace->v->get_values(), inc, workspace->temp1->get_values(), inc);
    if (!std::is_same<vtype_precond_apply, vtype>::value) {
        workspace->v_apply_->copy_from(workspace->temp1.get());
        auto precond_operator = static_cast<PrecondOperator<device, vtype_precond_apply, itype>*>(precond);
        precond_operator->apply(context, MagmaNoTrans, workspace->v_apply_.get());
        workspace->temp1->copy_from(workspace->v_apply_.get());
    }
    else {
        auto precond_operator = static_cast<PrecondOperator<device, vtype, itype>*>(precond);
        precond_operator->apply(context, MagmaNoTrans, workspace->temp1.get());
    }
    vtype one = 1.0;
    vtype zero = 0.0;
    vtype minus_one = -1.0;
    auto queue = context->get_queue();
    auto exec = context->get_executor();
    if (!std::is_same<vtype_internal, vtype>::value) {
        workspace->u_in->copy_from(workspace->u.get());
        workspace->v_in->copy_from(workspace->temp1.get());
        auto t = static_cast<matrix::Sparse<device, vtype_internal, itype>*>(workspace->mtx_apply.get());
        t->apply((vtype_internal)one, workspace->v_in.get(), (vtype_internal)minus_one, workspace->u_in.get());
        workspace->u->copy_from(workspace->u_in.get());
    } else {
        mtx_in->apply(one, workspace->temp1.get(), minus_one, workspace->u.get());
        cudaDeviceSynchronize();
    }
    workspace->beta = blas::norm2(context, num_rows, workspace->u->get_values(), 1);
    blas::scale(context, num_rows, one / workspace->beta, workspace->u->get_values(), inc);
    // Compute new v vector.
    if (!std::is_same<vtype_internal, vtype>::value) {
        workspace->u_in->copy_from(workspace->u.get());
        workspace->v_in->copy_from(workspace->temp1.get());
        auto t = static_cast<matrix::Sparse<device, vtype_internal, itype>*>(workspace->mtx_apply.get());
        t->transpose()->apply((vtype_internal)one, workspace->u_in.get(), (vtype_internal)zero, workspace->v_in.get());
        workspace->temp1->copy_from(workspace->v_in.get());
    } else {
        mtx_in->transpose()->apply(one, workspace->u.get(), zero, workspace->temp1.get());
    }

    if (!std::is_same<vtype_precond_apply, vtype>::value) {
        workspace->v_apply_->copy_from(workspace->temp1.get());
        auto precond_operator = static_cast<PrecondOperator<device, vtype_precond_apply, itype>*>(precond);
        precond_operator->apply(context, MagmaTrans, workspace->v_apply_.get());
        workspace->temp1->copy_from(workspace->v_apply_.get());
    }
    else {
        auto precond_operator = static_cast<PrecondOperator<device, vtype, itype>*>(precond);
        precond_operator->apply(context, MagmaTrans, workspace->temp1.get());
    }
    blas::axpy(context, num_cols, -(workspace->beta), workspace->v->get_values(), 1,
               workspace->temp1->get_values(), 1);
    workspace->alpha = blas::norm2(context, num_cols, workspace->temp1->get_values(), inc);
    blas::scale(context, num_cols, 1 / workspace->alpha, workspace->temp1->get_values(), inc);
    blas::copy(context, num_cols, workspace->temp1->get_values(), inc, workspace->v->get_values(), inc);
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void step_2(std::shared_ptr<Context<device>> context,
            iterative::LsqrConfig< vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>*
                config,
            iterative::Logger* logger,
            //PrecondOperator<device, vtype_precond_apply, itype>* precond,
            Preconditioner* precond,
            lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
            matrix::Sparse<device, vtype, itype>* mtx_in,
            matrix::Dense<device, vtype_refine>* sol_in)
{
    auto num_rows = mtx_in->get_size()[0];
    auto num_cols = mtx_in->get_size()[1];
    auto mtx = mtx_in->get_values();
    auto sol = sol_in->get_values();
    itype inc = 1;
    vtype one = 1.0;
    auto rho = std::sqrt(((workspace->rho_bar) * (workspace->rho_bar) +
                          workspace->beta * workspace->beta));
    auto c = (workspace->rho_bar) / rho;
    auto s = workspace->beta / rho;
    auto theta = s * workspace->alpha;
    workspace->rho_bar = -c * workspace->alpha;
    auto phi = c * (workspace->phi_bar);
    workspace->phi_bar = s * (workspace->phi_bar);
    if (!std::is_same<vtype, vtype_precond_apply>::value) {
        workspace->v_apply_->copy_from(workspace->w.get());
        auto precond_operator = static_cast<PrecondOperator<device, vtype_precond_apply, itype>*>(precond);
        precond_operator->apply(context, MagmaNoTrans, workspace->v_apply_.get());
        workspace->temp_refine_->copy_from(workspace->v_apply_.get());
    }
    else {
        blas::copy(context, num_cols, workspace->w->get_values(), inc, workspace->temp1->get_values(), inc);
        auto precond_operator = static_cast<PrecondOperator<device, vtype, itype>*>(precond);
        precond_operator->apply(context, MagmaNoTrans, workspace->temp1.get());
        workspace->temp_refine_->copy_from(workspace->temp1.get());
    }
    blas::axpy(context, num_cols, static_cast<vtype_refine>(phi / rho), workspace->temp_refine_->get_values(), 1, sol, 1);
    // Compute new vector w.
    blas::scale(context, num_cols, -(theta / rho), workspace->w->get_values(), inc);
    blas::axpy(context, num_cols, one, workspace->v->get_values(), 1, workspace->w->get_values(), 1);
}

template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
bool check_stopping_criteria(std::shared_ptr<Context<device>> context,
                             iterative::LsqrConfig<vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* config,
                             iterative::Logger* logger,
                             lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
                             matrix::Sparse<device, vtype_refine, itype>* mtx,
                             matrix::Dense<device, vtype_refine>* sol,
                             matrix::Dense<device, vtype_refine>* res)
{
    auto num_rows = mtx->get_size()[0];
    auto num_cols = mtx->get_size()[1];
    vtype_refine one = 1.0;
    vtype_refine minus_one = -1.0;
    mtx->apply(minus_one, sol, one, res);
    itype inc = 1;
    workspace->resnorm_previous = workspace->resnorm;
    workspace->resnorm = blas::norm2(context, num_rows, res->get_values(), inc);
    workspace->resnorm = workspace->resnorm / workspace->rhsnorm;
    logger->set_relres_history(workspace->completed_iterations, workspace->resnorm);
    double true_sol_norm = 0.0;
    if (logger->record_true_error()) {
        blas::copy(context, num_cols, sol->get_values(), 1, workspace->true_error_->get_values(), 1);
        blas::axpy(context, num_cols, minus_one, workspace->true_sol_->get_values(), 1, workspace->true_error_->get_values(), 1);
        auto t = blas::norm2(context, num_cols, workspace->true_sol_->get_values(), 1);
        true_sol_norm = t;
        auto true_error = blas::norm2(context, num_cols, workspace->true_error_->get_values(), 1)/t;
        logger->set_true_error_history(workspace->completed_iterations, true_error);  // @error
    }
    if (logger->record_noisy_error() && logger->record_true_error()) {
        blas::copy(context, num_cols, sol->get_values(), 1, workspace->true_error_->get_values(), 1);
        blas::axpy(context, num_cols, minus_one, workspace->noisy_sol_->get_values(), 1, workspace->true_error_->get_values(), 1);
        auto noisy_error = blas::norm2(context, num_cols, workspace->true_error_->get_values(), 1)/true_sol_norm;
        logger->set_noisy_error_history(workspace->completed_iterations, noisy_error);  // @error
        auto t0 = blas::norm2(context, num_cols, workspace->true_sol_->get_values(), 1);
        auto t1 = blas::norm2(context, num_cols, workspace->noisy_sol_->get_values(), 1);
        auto t2 = blas::norm2(context, num_cols, sol->get_values(), 1);
        auto similarity_t = blas::dot(context, num_cols, workspace->true_sol_->get_values(), 1,
           sol->get_values(), 1);
        similarity_t = similarity_t / (t0*t2);
        auto similarity_n = blas::dot(context, num_cols, sol->get_values(), 1,
           workspace->noisy_sol_->get_values(), 1);
        similarity_n = similarity_n / (t2*t1);
        //std::cout << workspace->completed_iterations << " / ";
        //std::cout << std::setprecision(15) << similarity_t << ", ";
        //std::cout << std::setprecision(15) << similarity_n << '\n';
        logger->set_similarity_history(workspace->completed_iterations, similarity_t, similarity_n);
    }
    else if (logger->record_noisy_error()) {
        blas::copy(context, num_cols, sol->get_values(), 1, workspace->true_error_->get_values(), 1);
        blas::axpy(context, num_cols, minus_one, workspace->noisy_sol_->get_values(), 1, workspace->true_error_->get_values(), 1);
        auto t = blas::norm2(context, num_cols, workspace->noisy_sol_->get_values(), 1);
        auto noisy_error = blas::norm2(context, num_cols, workspace->true_error_->get_values(), 1)/t;
        logger->set_noisy_error_history(workspace->completed_iterations, noisy_error);  // @error
    }
#if VISUALS
    std::cout << workspace->completed_iterations << " / ";
    std::cout << std::setprecision(15) << workspace->resnorm << '\n';
#endif
    auto stagnation_index = workspace->compute_stagnation_index();
    auto stagnates =
    (workspace->solver_stagnates(stagnation_index, config->get_stagnation_tolerance(),
        workspace->resnorm_previous)
            && workspace->new_restart_active
            && (workspace->completed_iterations_per_restart >= config->get_min_iterations()));
    if ((logger->record_stagnation()) && (workspace->new_restart_active)) {
        logger->set_stagnation_history(workspace->completed_iterations,
            stagnation_index / workspace->resnorm_previous);
    }
    if ((workspace->completed_iterations_per_restart >= config->get_iterations())
        || (workspace->resnorm < config->get_tolerance())
        || stagnates) {
        return true;
    }
    else {
        return false;
    }
    return true;
}


template <ContextType device,
          typename vtype,
          typename vtype_internal,
          typename vtype_precond_apply,
          typename vtype_refine,
          typename itype>
void run_lsqr(std::shared_ptr<Context<device>> context,
              iterative::LsqrConfig<vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* config,
              iterative::Logger* logger,
              Preconditioner* precond,
              lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>* workspace,
              matrix::Sparse<device, vtype_refine, itype>* mtx_refine,
              matrix::Dense<device, vtype_refine>* res_refine,
              matrix::Dense<device, vtype_refine>* sol_refine)
{
    auto mtx = static_cast<matrix::Sparse<device, vtype, itype>*>(workspace->mtx_.get());
    auto res = workspace->res_.get();
    auto rhs = workspace->rhs_refine_.get();
    if (check_stopping_criteria(context, config, logger, workspace, mtx_refine, sol_refine, rhs)) {
        return;
    }
    workspace->new_restart_active = true;
    initialize(context, config, logger, precond, workspace, mtx, res);
    while (1) {
        step_1(context, config, logger, precond, workspace, mtx);
        step_2(context, config, logger, precond, workspace, mtx, sol_refine);
        rhs->copy_from(res_refine);
        workspace->completed_iterations += 1;
        workspace->completed_iterations_per_restart += 1;
        if (check_stopping_criteria(context, config, logger, workspace, mtx_refine, sol_refine, rhs)) {
            workspace->new_restart_active = false;
            workspace->completed_restarts += 1;
            workspace->completed_iterations_per_restart = 0;
            res->copy_from(rhs);
            rhs->copy_from(res_refine);
            break;
        }
    }
    logger->set_completed_iterations(workspace->completed_iterations);
}

template void run_lsqr(std::shared_ptr<Context<CUDA>> context,
              iterative::LsqrConfig<double, double, double, double, magma_int_t>* config,
              iterative::Logger* logger,
              //PrecondOperator<CUDA, double, magma_int_t>* precond,
              Preconditioner* precond,
              lsqr::Workspace<CUDA, double, double, double, double, magma_int_t>* workspace,
              matrix::Sparse<CUDA, double, magma_int_t>* mtx,
              matrix::Dense<CUDA, double>* rhs,
              matrix::Dense<CUDA, double>* sol);


template class Lsqr<CUDA, double, double, double, double, magma_int_t>;
template class Lsqr<CUDA, double, double, float, double, magma_int_t>;
template class Lsqr<CUDA, double, float, float, double, magma_int_t>;
template class Lsqr<CUDA, double, float, double, double, magma_int_t>;
template class Lsqr<CUDA, float, float, double, double, magma_int_t>;
template class Lsqr<CUDA, float, float, float, double, magma_int_t>;
//template class Fgmres<CUDA, double, float, double, magma_int_t>;
// template class Fgmres<CUDA, double, __half, double, magma_int_t>;
//template class Fgmres<CUDA, double, float, float, magma_int_t>;
// template class Fgmres<CUDA, double, __half, __half, magma_int_t>;
template class Lsqr<CUDA, float, float, float, float, magma_int_t>;
// template class Fgmres<CUDA, float, __half, float, magma_int_t>;
//

template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::Lsqr(
     std::shared_ptr<Context<device>> context,
     std::shared_ptr<iterative::LsqrConfig<vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>> config,
     std::shared_ptr<MtxOp<device>> mtx,
     std::shared_ptr<matrix::Dense<device, vtype_refine>> sol,
     std::shared_ptr<matrix::Dense<device, vtype_refine>> rhs) : Solver<device>(mtx->get_context())
{
    context_ = context;
    config_  = config;
    mtx_ = mtx;
    rhs_ = rhs;
    sol_ = sol;
    logger_ = iterative::Logger::create(config_.get());
    generate();
}

template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
bool Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::stagnates()
{
    //return logger_->solver_stagnates(logger_.compute_stagnation_index(), config_->get_stagnation_tolerance(), config_->get_stagnation_weight());
    return true;
}

template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
bool Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::converges()
{
    return (workspace_->resnorm < config_->get_tolerance());
}


template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
void Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::run()
{
    auto context = this->context_;
    auto precond = config_->get_precond();
    if (auto t =
            dynamic_cast<matrix::Dense<device, vtype_refine>*>(mtx_.get());
        t != nullptr) {
        run_lsqr(context, config_.get(), logger_.get(),
            static_cast<PrecondOperator<device, vtype_precond_apply, itype>*>(precond.get()),
            workspace_.get(), t, rhs_.get(), sol_.get());
    } else if (auto t = dynamic_cast<matrix::Sparse<device, vtype_refine, itype>*>(
                   mtx_.get());
               t != nullptr) {
        run_lsqr(context, config_.get(), logger_.get(), precond.get(), workspace_.get(), t,
                 rhs_.get(), sol_.get());
    } else {
        // Run non-preconditioned Fgmres.
    }
}

template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
void Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::generate()
{
    auto size = mtx_->get_size();
    if (logger_->record_true_error() && (logger_->record_noisy_error())) {
        true_sol_ = rls::matrix::Dense<device, vtype_refine>::create(this->get_context(), logger_->get_filename_true_sol());
        auto noisy_sol = rls::share(rls::matrix::Dense<device, vtype_refine>::create(this->get_context(), logger_->get_filename_noisy_sol()));
        true_error_ = rls::matrix::Dense<device, vtype_refine>::create(this->get_context(), dim2(size[1], 1));
        workspace_ = lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(
            this->get_context(), mtx_, sol_, rhs_, true_sol_, true_error_, noisy_sol);
    }
    else if (logger_->record_true_error()) {
        true_sol_ = rls::matrix::Dense<device, vtype_refine>::create(this->get_context(), logger_->get_filename_true_sol());
        true_error_ = rls::matrix::Dense<device, vtype_refine>::create(this->get_context(), dim2(size[1], 1));
        workspace_ = lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(
            this->get_context(), mtx_, sol_, rhs_, true_sol_, true_error_);
    }
    else {
        //workspace_ = lsqr::Workspace<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::create(
        //    this->get_context(), mtx_, sol_, rhs_);
    }
    workspace_->rhsnorm = blas::norm2(context_, rhs_->get_size()[0], rhs_->get_values(), 1);
}

// @Change to -> iterative::Logger*
template<ContextType device,
         typename vtype,
         typename vtype_internal,
         typename vtype_precond_apply,
         typename vtype_refine,
         typename itype>
iterative::Logger Lsqr<device, vtype, vtype_internal, vtype_precond_apply, vtype_refine, itype>::get_logger()
{
    return *logger_;
}


}  // end of namespace solver
}  // end of namespace rls
