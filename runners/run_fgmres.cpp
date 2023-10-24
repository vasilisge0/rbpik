#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>


#include "../include/randls.hpp"


int precision_parser(std::string prec, std::string prec_in)
{
    if ((prec.compare("fp64") == 0) && (prec_in.compare("fp64") == 0)) {
        return 0;
    } else if ((prec.compare("fp64") == 0) && (prec_in.compare("fp32") == 0)) {
        return 1;
    } else if ((prec.compare("fp64") == 0) && (prec_in.compare("tf32") == 0)) {
        return 2;
    } else if ((prec.compare("fp64") == 0) && (prec_in.compare("fp16") == 0)) {
        return 3;
    } else if ((prec.compare("fp32") == 0) && (prec_in.compare("fp32") == 0)) {
        return 4;
    } else if ((prec.compare("fp32") == 0) && (prec_in.compare("tf32") == 0)) {
        return 5;
    } else if ((prec.compare("fp32") == 0) && (prec_in.compare("fp16") == 0)) {
        return 6;
    }
    return -1;
}

enum GlobalDataType { FP64, FP32 };


int main(int argc, char* argv[])
{
    std::vector<std::string> args;
    args.assign(argv, argv + argc);
    std::string input_runfile = args[0];
    std::string input_tol = args[1];
    std::string input_precond_prec = args[2];
    std::string input_precond_in_prec = args[3];
    std::string input_solver_prec = args[4];
    std::string input_solver_in_prec = args[5];
    std::string input_mtx = args[6];
    std::string input_rhs = args[7];
    std::string input_precond_type = args[8];
    std::string input_num_samples = args[9];
    std::string input_outfile = args[10];
    std::string input_warmup_iters = args[11];
    std::string input_runtime_iters = args[12];

    std::cout << "setting\n";
    std::cout << "-------\n";
    std::cout << "        input_runfile: " << input_runfile << '\n';
    std::cout << "            input_tol: " << input_tol << '\n';
    std::cout << "input_precond_in_prec: " << input_precond_in_prec << '\n';
    std::cout << "   input_precond_prec: " << input_precond_prec << '\n';
    std::cout << " input_solver_in_prec: " << input_solver_in_prec << '\n';
    std::cout << "    input_solver_prec: " << input_solver_prec << '\n';
    std::cout << "            input_mtx: " << input_mtx << '\n';
    std::cout << "   input_precond_type: " << input_precond_type << '\n';
    std::cout << "    input_num_samples: " << input_num_samples << '\n';
    std::cout << "        input_outfile: " << input_outfile << '\n';
    std::cout << "   input_warmup_iters: " << input_warmup_iters << '\n';
    std::cout << "  input_runtime_iters: " << input_runtime_iters << '\n';


//    // Decode input arguments.
//    double tol = std::atof(input_tol.c_str());
//    double sampling_coeff = std::atof(input_num_samples.c_str());
//    magma_int_t warmup_iters = std::atoi(input_warmup_iters.c_str());
//    magma_int_t runtime_iters = std::atoi(input_runtime_iters.c_str());
//    enum GlobalDataType data_type;
//    enum GlobalDataType data_type_precond;
//    enum GlobalDataType data_type_solver;
//    using dense = rls::matrix::Dense<rls::CUDA, double>;
//    auto context = rls::share(rls::Context<rls::CUDA>::create());
//    auto mtx = rls::share(dense::create(context, input_mtx));
//    auto rhs = rls::share(dense::create(context, input_rhs));
//    auto sol = rls::share(dense::create(context, rhs->get_size()));
//    sol->zeros();
//
//    //// Decides the precision of the gaussian preconditioner depending on the
//    //// inputs.
//    //auto precond_prec_type =
//    //    precision_parser(input_precond_prec, input_precond_in_prec);
//    std::shared_ptr<rls::Preconditioner> precond;
//    rls::preconditioner::logger precond_logger;
//    precond_logger.warmup_runs_ = warmup_iters;
//    precond_logger.runs_ = runtime_iters;
//    /// switch (precond_prec_type) {
//    ///     case 0:
//    ///     {
//    //
//    data_type_precond = FP64;
//    rls::preconditioner::generalized_split::Config<double, double, double, magma_int_t> config(sampling_coeff);
//    dim2 sketch_size = {static_cast<int>(std::ceil(sampling_coeff * mtx->get_size()[1])), mtx->get_size()[0]};
//    std::shared_ptr<rls::SketchOperator<rls::CUDA, double, magma_int_t>> sketch = rls::share(rls::GaussianSketch<rls::CUDA, double, double, magma_int_t>::create(context, sketch_size));
//    //std::string filename_sketch = "sketch.mtx";
//    //std::cout << "before reading sketch\n";
//    //std::shared_ptr<rls::SketchOperator<rls::CUDA, double, magma_int_t>> sketch = rls::share(rls::GaussianSketch<rls::CUDA, double, double, magma_int_t>::create(context, filename_sketch));
//
//    precond = rls::preconditioner::GeneralizedSplit<rls::CUDA, double, double, double, magma_int_t>::create(mtx, sketch, config);
//    precond->compute();
//
//    //break;
//    //     }
//    //     case 1:
//    //     {
//    //         data_type_precond = FP64;
//    //         // precond = rls::preconditioner::gaussian<float, double,
//    //         magma_int_t>::create(mtx); precond =
//    //         rls::preconditioner::GeneralizedSplit<float, double,
//    //         magma_int_t>::create(mtx, sampling_coeff); break;
//    //     }
//    //     case 2:
//    //     {
//    //         data_type_precond = FP64;
//    //         // precond = rls::preconditioner::gaussian<float, float,
//    //         magma_int_t>::create(mtx); context->enable_tf32_flag(); precond =
//    //         rls::preconditioner::GeneralizedSplit<float, double,
//    //         magma_int_t>::create(mtx, sampling_coeff);
//    //         context->disable_tf32_flag();
//    //         break;
//    //     }
//    //     case 3:
//    //     {
//    //         data_type_precond = FP64;
//    //         precond = rls::preconditioner::GeneralizedSplit<__half, double,
//    //         magma_int_t>::create(mtx, sampling_coeff); break;
//    //     }
//    //     case 4:
//    //     {
//    //         data_type_precond = FP32;
//    //         std::shared_ptr<rls::matrix::Dense<float, rls::CUDA>> mtx_fp32 =
//    //         rls::matrix::Dense<float, rls::CUDA>::create(context);
//    //         mtx_fp32->copy_from(mtx);
//    //         precond = rls::preconditioner::GeneralizedSplit<float, float,
//    //         magma_int_t>::create(std::move(mtx_fp32)); break;
//    //     }
//    //     case 5:
//    //     {
//    //         data_type_precond = FP64;
//    //         // precond = rls::preconditioner::gaussian<float, float,
//    //         magma_int_t>::create(mtx); break;
//    //     }
//    //     case 6:
//    //     {
//    //         data_type_precond = FP64;
//    //         // precond = rls::preconditioner::gaussian<float, float,
//    //         magma_int_t>::create(mtx); break;
//    //     }
//    //     default:
//    //         break;
//    // }
//
//    //// Decides the precision of the solver preconditioner depending on the
//    /// inputs.
//    //auto solver_prec_type = precision_parser(input_solver_prec, input_solver_in_prec);
//    using Fgmres = rls::solver::Fgmres<rls::CUDA, double, double, double, magma_int_t>;
//    using FgmresConfig = rls::solver::iterative::FgmresConfig<double, double, double, magma_int_t>;
//    using Solver = rls::Solver<rls::CUDA>;
//    FgmresConfig solver_config;
//    solver_config.set_precond(precond);
//    solver_config.set_tolerance(tol);
//    solver_config.set_iterations(150);
//
//    // pass iterations via config not logger
//
//    //auto precond_mtx = static_cast<rls::PrecondOperator<rls::CUDA, double, magma_int_t>*>(precond.get());
//
//    //auto sop = static_cast<rls::SketchOperator<rls::CUDA, double, magma_int_t>*>(sketch.get());
//    auto out_mtx = sketch->get_mtx();
//    char* filename_precond = "test_small_precond.mtx";
//std::cout << "before write" << '\n';
//
//    {
//        auto size = out_mtx->get_size();
//        std::cout << "size[0]: " << size[0] << '\n';
//        std::cout << "size[1]: " << size[1] << '\n';
//        std::cout << "mtx->get_size()[0]: " << mtx->get_size()[0] << '\n';
//        std::cout << "mtx->get_size()[1]: " << mtx->get_size()[1] << '\n';
//        std::cout << "mtx: \n";
//        //rls::io::print_mtx_gpu(5, 3, mtx->get_values(), 5, context->get_queue());
//    }
////rls::io::write_mtx(filename_precond, out_mtx->get_size()[0], out_mtx->get_size()[1], out_mtx->get_values(), out_mtx->get_size()[0], context->get_queue());
////std::cout << "after write" << '\n';
//
// std::shared_ptr<Solver> solver = rls::share(Fgmres::create(context, solver_config, mtx, sol, rhs));
// solver->run();
//    //rls::solver::logger solver_logger; solver_logger.warmup_runs_ =
//    // warmup_iters; solver_logger.runs_ = runtime_iters; 
//    //  switch
//    // (solver_prec_type) {
//    //     case 0:
//    //     {
//    //         data_type_solver = FP64;
//    //         solver = rls::solver::Fgmres<double, double, magma_int_t,
//    //         rls::CUDA>::create(precond.get(), mtx, rhs, tol); break;
//    //     }
//    //     case 1:
//    //     {
//    //         data_type_solver = FP64;
//    //         solver = rls::solver::Fgmres<float, double, magma_int_t,
//    //         rls::CUDA>::create(precond.get(), mtx, rhs, tol); break;
//    //     }
//    //     case 2:
//    //     {
//    //         data_type_solver = FP64;
//    //         context->enable_tf32_flag();
//    //         solver = rls::solver::Fgmres<float, double, magma_int_t,
//    //         rls::CUDA>::create(precond.get(), mtx, rhs, tol);
//    //         context->disable_tf32_flag();
//    //         break;
//    //     }
//    //     case 3:
//    //     {
//    //         data_type_solver = FP64;
//    //         solver = rls::solver::Fgmres<__half, double, magma_int_t,
//    //         rls::CUDA>::create(precond.get(), mtx, rhs, tol); break;
//    //     }
//    //     case 4:
//    //     {
//    //         data_type_solver = FP32;
//    //         std::shared_ptr<rls::matrix::Dense<float, rls::CUDA>> mtx_fp32 =
//    //         rls::matrix::Dense<float, rls::CUDA>::create(context);
//    //         std::shared_ptr<rls::matrix::Dense<float, rls::CUDA>> rhs_fp32 =
//    //         rls::matrix::Dense<float, rls::CUDA>::create(context);
//    //         mtx_fp32->copy_from(mtx);
//    //         rhs_fp32->copy_from(rhs);
//    //         solver = rls::solver::Fgmres<float, float, magma_int_t,
//    //         rls::CUDA>::create(precond.get(), std::move(mtx_fp32),
//    //         std::move(rhs_fp32), tol); break;
//    //     }
//    //     case 5:
//    //     {
//    //         data_type_solver = FP64;
//    //         // solver = rls::solver::lsqr<__half, float,
//    //         magma_int_t>::create(precond.get(), mtx, rhs, tol); break;
//    //     }
//    //     case 6:
//    //     {
//    //         data_type_solver = FP64;
//    //         // solver = rls::solver::lsqr<float, float,
//    //         magma_int_t>::create(precond.get(), mtx, rhs, tol); break;
//    //     }
//    //     default:
//    //         break;
//    // }
//    // solver->set_logger(solver_logger);
//
//    // solver->generate();
//    // solver->run();
//
//    // std::cout << "precond->runtime_: " << precond->get_runtime() << "\n";
//    // std::cout << " solver->runtime_: " << solver->get_runtime() << "\n";
//    // std::cout << "    solver->iter_: " << solver->get_iterations_completed()
//    // << "\n"; std::cout << "  solver->resnorm: " << solver->get_resnorm() <<
//    // '\n';
//
//    // rls::io::write_output(input_outfile.c_str(), mtx->get_size()[0],
//    // mtx->get_size()[1], solver->get_max_iter(),
//    //     sampling_coeff, std::ceil(sampling_coeff * mtx->get_size()[1]),
//    //     precond->get_runtime(), solver->get_runtime(), precond->get_runtime()
//    //     + solver->get_runtime(), solver->get_iterations_completed(),
//    //     solver->get_resnorm());

    return 0;
}
