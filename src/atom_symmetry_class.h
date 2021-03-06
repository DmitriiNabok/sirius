#ifndef __ATOM_SYMMETRY_CLASS_H__
#define __ATOM_SYMMETRY_CLASS_H__

/** \page page1 Notes
    
    AtomSymmetryClass holds information about radial functions.
*/

/** \page page2 Standard variable names

     idxlo - index of local orbital \n
     idxrf - index of radial function \n
     ia - index of atom \n
     ic - index of atom class \n
     iat - index of atom type \n
     ir - index of r-point \n
     ig - index of G-vector \n
*/

namespace sirius {

class AtomSymmetryClass
{
    private:
        
        /// symmetry class id in the range [0, N_class - 1]
        int id_;

        /// list of atoms of this class
        std::vector<int> atom_id_;
        
        /// atom type
        AtomType* atom_type_;

        /// spherical part of effective potential 
        std::vector<double> spherical_potential_;

        /// list of radial functions
        mdarray<double, 3> radial_functions_;
        
        /// surface derivatives of aw radial functions
        mdarray<double, 2> aw_surface_derivatives_;

        /// spherical part of radial integral
        mdarray<double, 2> h_spherical_integrals_;

        /// overlap integrals
        mdarray<double, 3> o_radial_integrals_;

        /// spin-orbit interaction integrals
        mdarray<double, 3> so_radial_integrals_;

        /// core charge density
        std::vector<double> core_charge_density_;

        /// core eigen-value sum
        double core_eval_sum_;

        /// core leakage
        double core_leakage_;
        
        /// list of radial descriptor sets used to construct augmented waves 
        std::vector<radial_solution_descriptor_set> aw_descriptors_;
        
        /// list of radial descriptor sets used to construct local orbitals
        std::vector<radial_solution_descriptor_set> lo_descriptors_;
        
        void generate_aw_radial_functions()
        {
            int nmtp = atom_type_->num_mt_points();
           
            RadialSolver solver(false, -1.0 * atom_type_->zn(), atom_type_->radial_grid());
            
            #pragma omp parallel default(shared)
            {
                Spline<double> s(nmtp, atom_type_->radial_grid());
                
                std::vector<double> p;
                std::vector<double> hp;
                
                double dpdr[atom_type_->max_aw_order()];
                
                #pragma omp for schedule(dynamic, 1)
                for (int l = 0; l < num_aw_descriptors(); l++)
                {
                    for (int order = 0; order < (int)aw_descriptor(l).size(); order++)
                    {
                        radial_solution_descriptor& rsd = aw_descriptor(l)[order];

                        int idxrf = atom_type_->indexr().index_by_l_order(l, order);

                        // find linearization energies
                        if (rsd.auto_enu == 1 || rsd.auto_enu == 2) 
                        {
                            solver.bound_state(rsd.n, rsd.l, spherical_potential_, rsd.enu, p);
                            if (rsd.auto_enu == 2) rsd.enu = solver.find_enu(rsd.l, spherical_potential_, rsd.enu);
                        }

                        solver.solve_in_mt(rsd.l, rsd.enu, rsd.dme, spherical_potential_, p, hp, dpdr[order]);

                        // normalize
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(p[ir], 2);
                        double norm = 1.0 / sqrt(s.interpolate().integrate(0));

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf, 0) = p[ir] * norm;
                            radial_functions_(ir, idxrf, 1) = hp[ir] * norm;
                        }
                        dpdr[order] *= norm;

                        // orthogonalize
                        for (int order1 = 0; order1 < order; order1++)
                        {
                            int idxrf1 = atom_type_->indexr().index_by_l_order(l, order1);

                            for (int ir = 0; ir < nmtp; ir++) 
                                s[ir] = radial_functions_(ir, idxrf, 0) * radial_functions_(ir, idxrf1, 0);

                            double t1 = s.interpolate().integrate(0);

                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                radial_functions_(ir, idxrf, 0) -= radial_functions_(ir, idxrf1, 0) * t1;
                                radial_functions_(ir, idxrf, 1) -= radial_functions_(ir, idxrf1, 1) * t1;
                            }
                            dpdr[order] -= t1 * dpdr[order1];
                        }

                        // normalize again
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(radial_functions_(ir, idxrf, 0), 2);
                        norm = s.interpolate().integrate(0);

                        if (fabs(norm) < 1e-10) error(__FILE__, __LINE__, "aw radial functions are linearly dependent");

                        norm = 1.0 / sqrt(norm);

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf, 0) *= norm;
                            radial_functions_(ir, idxrf, 1) *= norm;
                        }
                        dpdr[order] *= norm;

                        // radial derivative
                        //double rderiv = (radial_functions_(nmtp - 1, idxrf, 0) - radial_functions_(nmtp - 2, idxrf, 0)) / 
                        //                atom_type_->radial_grid().dr(nmtp - 2);
                        
                        double rderiv = dpdr[order];
                        double R = atom_type_->mt_radius();

                        aw_surface_derivatives_(order, l) = (rderiv - radial_functions_(nmtp - 1, idxrf, 0) / R) / R;
                        
                        if (debug_level > 1)
                        {
                            printf("atom class id : %i  l : %i  order : %i  radial function value at MT : %f\n", 
                                   id_, l, order, radial_functions_(nmtp - 1, idxrf, 0));
                        }
                    }
                }
            }
        }

        void generate_lo_radial_functions()
        {
            int nmtp = atom_type_->num_mt_points();
            RadialSolver solver(false, -1.0 * atom_type_->zn(), atom_type_->radial_grid());
            
            #pragma omp parallel default(shared)
            {
                Spline<double> s(nmtp, atom_type_->radial_grid());
                
                double a[4][4];

                #pragma omp for schedule(dynamic, 1)
                for (int idxlo = 0; idxlo < num_lo_descriptors(); idxlo++)
                {
                    assert(lo_descriptor(idxlo).size() < 4);

                    int idxrf = atom_type_->indexr().index_by_idxlo(idxlo);

                    if (lo_descriptor(idxlo)[0].type == radial_solution)
                    {

                        std::vector< std::vector<double> > p(lo_descriptor(idxlo).size());
                        std::vector< std::vector<double> > hp(lo_descriptor(idxlo).size());

                        for (int order = 0; order < (int)lo_descriptor(idxlo).size(); order++)
                        {
                            radial_solution_descriptor& rsd = lo_descriptor(idxlo)[order];
                            
                            // find linearization energies
                            if (rsd.auto_enu == 1 || rsd.auto_enu == 2) 
                            {
                                solver.bound_state(rsd.n, rsd.l, spherical_potential_, rsd.enu, p[order]);
                                if (rsd.auto_enu == 2) rsd.enu = solver.find_enu(rsd.l, spherical_potential_, rsd.enu);
                            }

                            double dpdr;
                            solver.solve_in_mt(rsd.l, rsd.enu, rsd.dme, spherical_potential_, p[order], hp[order], dpdr); 
                            
                            // normalize radial solutions and divide by r
                            for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(p[order][ir], 2);
                            double norm = 1.0 / sqrt(s.interpolate().integrate(0));

                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                p[order][ir] *= (norm / atom_type_->radial_grid(ir));
                                hp[order][ir] *= norm; // don't divide hp by r
                                s[ir] = p[order][ir];
                            }

                            s.interpolate();
                            
                            // compute radial derivatives
                            for (int dm = 0; dm < (int)lo_descriptor(idxlo).size(); dm++) 
                                a[order][dm] = s.deriv(dm, nmtp - 1);
                        }

                        double b[] = {0.0, 0.0, 0.0, 0.0};
                        b[lo_descriptor(idxlo).size() - 1] = 1.0;

                        int info = linalg<lapack>::gesv((int)lo_descriptor(idxlo).size(), 1, &a[0][0], 4, b, 4);

                        if (info) 
                        {
                            std::stringstream s;
                            s << "gesv returned " << info;
                            error(__FILE__, __LINE__, s);
                        }
                        
                        // take linear combination of radial solutions
                        for (int order = 0; order < (int)lo_descriptor(idxlo).size(); order++)
                        {
                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                radial_functions_(ir, idxrf, 0) += b[order] * p[order][ir];
                                radial_functions_(ir, idxrf, 1) += b[order] * hp[order][ir];
                            }
                        }

                        // normalize
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(radial_functions_(ir, idxrf, 0), 2);
                        double norm = 1.0 / sqrt(s.interpolate().integrate(2));

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf, 0) *= norm;
                            radial_functions_(ir, idxrf, 1) *= norm;
                        }
                        
                        if (fabs(radial_functions_(nmtp - 1, idxrf, 0)) > 1e-10)
                        {
                            std::stringstream s;
                            s << "local orbital " << idxlo << " is not zero at MT boundary" << std::endl 
                              << "  value : " << radial_functions_(nmtp - 1, idxrf, 0);
                            error(__FILE__, __LINE__, s);
                        }
                    }

                    if (lo_descriptor(idxlo)[0].type == polynom)
                    {
                        int l = lo_descriptor(idxlo)[0].l;
                        int p1 = lo_descriptor(idxlo)[0].p1;
                        int p2 = lo_descriptor(idxlo)[0].p2;
                        double R = atom_type_->mt_radius();

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            double r = atom_type_->radial_grid(ir);
                            radial_functions_(ir, idxrf, 0) = pow(r, p1) * pow(1.0 - pow(r / R, 2), p2);
                            radial_functions_(ir, idxrf, 1) = -0.5 * ((-4*(1 + p1)*p2*pow(r,1 + p1)*pow(1 - pow(r,2)/pow(R,2),-1 + p2))/pow(R,2) + 
   p1*(1 + p1)*pow(r,-1 + p1)*pow(1 - pow(r,2)/pow(R,2),p2) + 
   pow(r,1 + p1)*((4*(-1 + p2)*p2*pow(r,2)* pow(1 - pow(r,2)/pow(R,2),-2 + p2))/pow(R,4) - 
      (2*p2*pow(1 - pow(r,2)/pow(R,2),-1 + p2))/pow(R,2))) + 
                            
                             (0.5 * l * (l + 1) / pow(r, 2) + spherical_potential_[ir]) * (radial_functions_(ir, idxrf, 0) * r);
                        }
                        
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(radial_functions_(ir, idxrf, 0), 2);
                        double norm = 1.0 / sqrt(s.interpolate().integrate(2));

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf, 0) *= norm;
                            radial_functions_(ir, idxrf, 1) *= norm;
                        }
                    }
                }
            }
            
            if (debug_level > 0 && num_lo_descriptors() > 0)
            {
                Spline<double> s(nmtp, atom_type_->radial_grid());
                mdarray<double, 2> loprod(num_lo_descriptors(), num_lo_descriptors());
                mdarray<complex16, 2> loprod_tmp(num_lo_descriptors(), num_lo_descriptors());
                for (int idxlo1 = 0; idxlo1 < num_lo_descriptors(); idxlo1++)
                {
                    int idxrf1 = atom_type_->indexr().index_by_idxlo(idxlo1);
                    radial_solution_descriptor rsd1 = lo_descriptor(idxlo1)[0];
                    
                    for (int idxlo2 = 0; idxlo2 < num_lo_descriptors(); idxlo2++)
                    {
                        int idxrf2 = atom_type_->indexr().index_by_idxlo(idxlo2);
                        radial_solution_descriptor rsd2 = lo_descriptor(idxlo2)[0];
                        
                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            s[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0);
                        }
                        s.interpolate();
                        if (rsd1.l == rsd2.l)
                        {
                            loprod(idxlo1, idxlo2) = s.integrate(2);
                        }
                        else
                        {
                            loprod(idxlo1, idxlo2) = 0.0;
                        }
                        loprod_tmp(idxlo1, idxlo2) = complex16(loprod(idxlo1, idxlo2), 0);
                    }
                }
                    
                standard_evp_lapack stdevp;

                std::vector<double> loprod_eval(num_lo_descriptors());
                mdarray<complex16, 2> loprod_evec(num_lo_descriptors(), num_lo_descriptors());
    
                stdevp.solve(num_lo_descriptors(), loprod_tmp.get_ptr(), loprod_tmp.ld(), &loprod_eval[0], 
                             loprod_evec.get_ptr(), loprod_evec.ld());

                double det = 1.0;
                for (int i = 0; i < num_lo_descriptors(); i++) det *= loprod_eval[i];

                if (fabs(det) < 0.001) 
                {
                    printf("\n");
                    printf("local orbitals for atom symmetry class %i are nearly degenerate\n", id_);
                    printf("local orbitals overlap matrix:\n");
                    for (int i = 0; i < num_lo_descriptors(); i++)
                    {
                        for (int j = 0; j < num_lo_descriptors(); j++) printf("%12.6f", loprod(i, j));
                        printf("\n");
                    }
                }
            }

            if (verbosity_level > 0)
            {
                std::stringstream s;
                s << "local_orbitals_" << id_ << ".dat";
                FILE* fout = fopen(s.str().c_str(), "w");

                for (int ir = 0; ir <atom_type_->num_mt_points(); ir++)
                {
                    fprintf(fout, "%f ", atom_type_->radial_grid(ir));
                    for (int idxlo = 0; idxlo < num_lo_descriptors(); idxlo++)
                    {
                        int idxrf = atom_type_->indexr().index_by_idxlo(idxlo);
                        fprintf(fout, "%f ", radial_functions_(ir, idxrf, 0));
                    }
                    fprintf(fout, "\n");
                }
                fclose(fout);
                
                s.str("");
                s << "local_orbitals_h_" << id_ << ".dat";
                fout = fopen(s.str().c_str(), "w");

                for (int ir = 0; ir <atom_type_->num_mt_points(); ir++)
                {
                    fprintf(fout, "%f ", atom_type_->radial_grid(ir));
                    for (int idxlo = 0; idxlo < num_lo_descriptors(); idxlo++)
                    {
                        int idxrf = atom_type_->indexr().index_by_idxlo(idxlo);
                        fprintf(fout, "%f ", radial_functions_(ir, idxrf, 1));
                    }
                    fprintf(fout, "\n");
                }
                fclose(fout);
            }
        }
        
        void transform_radial_functions(bool ort_lo, bool ort_aw)
        {
            Timer t("sirius::AtomSymmetryClass::transform_radial_functions");

            int nmtp = atom_type_->num_mt_points();
            Spline<double> s(nmtp, atom_type_->radial_grid());
            for (int l = 0; l <= atom_type_->indexr().lmax_lo(); l++)
            {
                // if we have local orbitals for the given l
                if ((atom_type_->indexr().num_lo(l) > 1) && ort_lo)
                {
                    int naw = (atom_type_->num_aw_descriptors() > l) ? (int)atom_type_->aw_descriptor(l).size() : 0;

                    // orthogonalize local orbitals
                    for (int order1 = 1; order1 < atom_type_->indexr().num_lo(l); order1++)
                    {
                        int idxrf1 = atom_type_->indexr().index_by_l_order(l, naw + order1);

                        for (int order2 = 0; order2 < order1; order2++)
                        {
                            int idxrf2 = atom_type_->indexr().index_by_l_order(l, naw + order2);

                            for (int ir = 0; ir < nmtp; ir++)
                                s[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0);
                            double t1 = s.interpolate().integrate(2);
                                
                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                radial_functions_(ir, idxrf1, 0) -= radial_functions_(ir, idxrf2, 0) * t1;
                                radial_functions_(ir, idxrf1, 1) -= radial_functions_(ir, idxrf2, 1) * t1;
                            }
                        }
                            
                        // normalize again
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(radial_functions_(ir, idxrf1, 0), 2);
                        double norm = s.interpolate().integrate(2);

                        if (fabs(norm) < 1e-10) 
                        {
                            std::stringstream s;
                            s << "local orbital radial function for l = " << l << ", order = " << order1 << 
                                 " is linearly dependent";
                            error(__FILE__, __LINE__, s);
                        }

                        norm = 1.0 / sqrt(norm);

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf1, 0) *= norm;
                            radial_functions_(ir, idxrf1, 1) *= norm;
                        }
                    }
                }
                
                if ((atom_type_->indexr().num_lo(l) > 0) && ort_aw)
                {
                    int naw = (int)atom_type_->aw_descriptor(l).size();

                    // orthogonalize aw functions to local orbitals
                    for (int order1 = 0; order1 < naw; order1++)
                    {
                        int idxrf1 = atom_type_->indexr().index_by_l_order(l, order1);
                        for (int order2 = 0; order2 < atom_type_->indexr().num_lo(l); order2++)
                        {
                            int idxrf2 = atom_type_->indexr().index_by_l_order(l, naw + order2);
                            
                            for (int ir = 0; ir < nmtp; ir++)
                                s[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0);
                            s.interpolate();
                            double t1 = s.integrate(1);
                                
                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                radial_functions_(ir, idxrf1, 0) -= atom_type_->radial_grid(ir) * 
                                                                    radial_functions_(ir, idxrf2, 0) * t1;
                                radial_functions_(ir, idxrf1, 1) -= radial_functions_(ir, idxrf2, 1) * t1;
                            }
                        }
                            
                        // normalize again
                        for (int ir = 0; ir < nmtp; ir++) s[ir] = pow(radial_functions_(ir, idxrf1, 0), 2);
                        double norm = s.interpolate().integrate(0);

                        if (fabs(norm) < 1e-10) 
                            error(__FILE__, __LINE__, "aw radial function is linearly dependent");

                        norm = 1.0 / sqrt(norm);

                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            radial_functions_(ir, idxrf1, 0) *= norm;
                            radial_functions_(ir, idxrf1, 1) *= norm;
                        }

                        error(__FILE__, __LINE__, "first, fix the radial derivative");
                        
                        // this is not precise
                        double rderiv = (radial_functions_(nmtp - 1, idxrf1, 0) - radial_functions_(nmtp - 2, idxrf1, 0)) / 
                                        atom_type_->radial_grid().dr(nmtp - 2);
                        double R = atom_type_->mt_radius();

                        aw_surface_derivatives_(order1, l) = (rderiv - radial_functions_(nmtp - 1, idxrf1, 0) / R) / R;
                    }

                }
            }
           
            // divide by r
            for (int l = 0; l < num_aw_descriptors(); l++)
            {
                for (int order = 0; order < (int)aw_descriptor(l).size(); order++)
                {
                    int idxrf = atom_type_->indexr().index_by_l_order(l, order);
                    for (int ir = 0; ir < nmtp; ir++)
                    {
                        radial_functions_(ir, idxrf, 0) /= atom_type_->radial_grid(ir);
                    }
                }
            }
        }

    public:
    
        AtomSymmetryClass(int id_, AtomType* atom_type_) : 
            id_(id_), atom_type_(atom_type_), core_eval_sum_(0.0), core_leakage_(0.0)
        {
        }

        void init()
        {
            aw_surface_derivatives_.set_dimensions(atom_type_->max_aw_order(), atom_type_->num_aw_descriptors());
            aw_surface_derivatives_.allocate();

            radial_functions_.set_dimensions(atom_type_->num_mt_points(), atom_type_->mt_radial_basis_size(), 2);
            radial_functions_.allocate();

            h_spherical_integrals_.set_dimensions(atom_type_->mt_radial_basis_size(), 
                                                  atom_type_->mt_radial_basis_size());
            h_spherical_integrals_.allocate();
            
            o_radial_integrals_.set_dimensions(atom_type_->indexr().lmax() + 1, atom_type_->indexr().max_num_rf(), 
                                               atom_type_->indexr().max_num_rf());
            o_radial_integrals_.allocate();
            
            so_radial_integrals_.set_dimensions(atom_type_->indexr().lmax() + 1, atom_type_->indexr().max_num_rf(), 
                                                atom_type_->indexr().max_num_rf());
            so_radial_integrals_.allocate();

            // copy descriptors because enu is defferent between atom classes
            aw_descriptors_.resize(atom_type_->num_aw_descriptors());
            for (int i = 0; i < num_aw_descriptors(); i++) aw_descriptors_[i] = atom_type_->aw_descriptor(i);

            lo_descriptors_.resize(atom_type_->num_lo_descriptors());
            for (int i = 0; i < num_lo_descriptors(); i++) lo_descriptors_[i] = atom_type_->lo_descriptor(i);
            
            core_charge_density_.resize(atom_type_->radial_grid().size());
            memset(&core_charge_density_[0], 0, atom_type_->radial_grid().size() * sizeof(double));
        }

        inline int id()
        {
            return id_;
        }

        inline void add_atom_id(int _atom_id)
        {
            atom_id_.push_back(_atom_id);
        }
        
        inline int num_atoms()
        {
            return (int)atom_id_.size();
        }

        inline int atom_id(int idx)
        {
            return atom_id_[idx];
        }

        void set_spherical_potential(std::vector<double>& veff)
        {
            int nmtp = atom_type_->num_mt_points();
            assert((int)veff.size() == nmtp);

            spherical_potential_.resize(atom_type_->radial_grid().size());
            
            // take current effective potential inside MT
            for (int ir = 0; ir < nmtp; ir++) spherical_potential_[ir] = veff[ir];

            // take potential of the free atom outside MT
            for (int ir = nmtp; ir < atom_type_->radial_grid().size(); ir++)
            {
                spherical_potential_[ir] = atom_type_->free_atom_potential(ir) - 
                                           (atom_type_->free_atom_potential(nmtp - 1) - veff[nmtp - 1]);
            }
        }

        void generate_radial_functions()
        {
            Timer t("sirius::AtomSymmetryClass::generate_radial_functions");

            radial_functions_.zero();

            generate_aw_radial_functions();
            generate_lo_radial_functions();
            transform_radial_functions(true, false);
            
            if (verbosity_level > 0)
            {
                std::stringstream s;
                s << "radial_functions_" << id_ << ".dat";
                FILE* fout = fopen(s.str().c_str(), "w");

                for (int ir = 0; ir <atom_type_->num_mt_points(); ir++)
                {
                    fprintf(fout, "%f ", atom_type_->radial_grid(ir));
                    for (int idxrf = 0; idxrf < atom_type_->indexr().size(); idxrf++)
                    {
                        fprintf(fout, "%f ", radial_functions_(ir, idxrf, 0));
                    }
                    fprintf(fout, "\n");
                }
                fclose(fout);
            }
        }

        inline void sync_radial_functions(int rank)
        {
            // don't broadcast Hamiltonian radial functions, because they are used locally
            int size = (int)radial_functions_.size(0) * radial_functions_.size(1);
            Platform::bcast(radial_functions_.get_ptr(), size, rank);
            Platform::bcast(aw_surface_derivatives_.get_ptr(), (int)aw_surface_derivatives_.size(), rank);
        }

        /// Generate radial overlap and SO integrals
        /** In the case of spin-orbit interaction the following integrals are computed:
            \f[
                \int f_{p}(r) \Big( \frac{1}{(2 M c)^2} \frac{1}{r} \frac{d V}{d r} \Big) f_{p'}(r) r^2 dr
            \f]
            
            Relativistic mass M is defined as
            \f[
                M = 1 - \frac{1}{2 c^2} V
            \f]
        */
        void generate_radial_integrals()
        {
            Timer t("sirius::AtomSymmetryClass::generate_radial_integrals");

            int nmtp = atom_type_->num_mt_points();
            Spline<double> s(nmtp, atom_type_->radial_grid()); 

            h_spherical_integrals_.zero();
            for (int i1 = 0; i1 < atom_type_->mt_radial_basis_size(); i1++)
            {
                for (int i2 = 0; i2 < atom_type_->mt_radial_basis_size(); i2++)
                {
                    // for spherical part of potential integrals are diagonal in l
                    if (atom_type_->indexr(i1).l == atom_type_->indexr(i2).l)
                    {
                        for (int ir = 0; ir < nmtp; ir++)
                        {
                            s[ir] = radial_functions_(ir, i1, 0) * radial_functions_(ir, i2, 1) * 
                                    atom_type_->radial_grid(ir); // hp was not divided by r, so we use r^{1}dr
                        }
                        h_spherical_integrals_(i1, i2) = s.interpolate().integrate(0) / y00;
                    }
                }
            }

            // TODO: kinetic energy for s-local orbitals? The best way to do it.
            // Problem: <s1 | \Delta | s2> != <s2 | \Delta | s1> 

            // check and symmetrize the radial integrals
            for (int idxlo1 = 0; idxlo1 < atom_type_->num_lo_descriptors(); idxlo1++)
            {
                int idxrf1 = atom_type_->indexr().index_by_idxlo(idxlo1);

                for (int idxlo2 = 0; idxlo2 <= idxlo1; idxlo2++)
                {
                    int idxrf2 = atom_type_->indexr().index_by_idxlo(idxlo2);

                    double diff = fabs(h_spherical_integrals_(idxrf1, idxrf2) - 
                                       h_spherical_integrals_(idxrf2, idxrf1));

                    if (verbosity_level > 0 && diff > 1e-12)
                    {
                        int l = atom_type_->indexr(idxrf2).l;
                        std::stringstream s;
                        s << "Wrong local orbital radial integrals for atom class " << id_ << ", l = " << l << std::endl
                          << " idxlo1 = " << idxlo1 << std::endl
                          << " idxlo2 = " << idxlo2 << std::endl
                          << " h(1,2) - h(2,1) = " << diff;

                        warning(__FILE__, __LINE__, s, 0);
                    }

                    if (true)
                    {
                        double avg = 0.5 * (h_spherical_integrals_(idxrf1, idxrf2) + 
                                            h_spherical_integrals_(idxrf2, idxrf1));
                        h_spherical_integrals_(idxrf1, idxrf2) = avg;
                        h_spherical_integrals_(idxrf2, idxrf1) = avg;
                    }
                }
            }
                
            for (int i2 = 0; i2 < atom_type_->mt_radial_basis_size() - atom_type_->num_lo_descriptors(); i2++)
            {
                int l = atom_type_->indexr(i2).l;
                int order2 = atom_type_->indexr(i2).order;
                
                for (int i1 = 0; i1 <= i2; i1++)
                {
                    if (atom_type_->indexr(i1).l == l)
                    {
                        int order1 = atom_type_->indexr(i1).order;

                        double R2 = pow(atom_type_->mt_radius(), 2);

                        double surf12 = 0.5 * aw_surface_dm(l, order1, 0) * aw_surface_dm(l, order2, 1) * R2;
                        double surf21 = 0.5 * aw_surface_dm(l, order2, 0) * aw_surface_dm(l, order1, 1) * R2;

                        double v1 = y00 * h_spherical_integrals_(i1, i2) + surf12;
                        double v2 = y00 * h_spherical_integrals_(i2, i1) + surf21; 

                        double diff = fabs(v1 - v2);
                        if (verbosity_level > 0 && diff > 1e-12)
                        {
                            std::stringstream s;
                            s << "Wrong radial integrals for atom class " << id_ << ", l = " << l << std::endl
                              << " order1 = " << order1 << ", value = " << v1 << std::endl
                              << " order2 = " << order2 << ", value = " << v2 << std::endl
                              << " <u_{l1,o1}| T |u_{l2,o2}> - <u_{l2,o2}| T |u_{l1,o1}> = " << diff << std::endl
                              << " h(1,2) = " << y00 * h_spherical_integrals_(i1, i2) << std::endl
                              << " h(2,1) = " << y00 * h_spherical_integrals_(i2, i1) << std::endl 
                              << " h(1,1) = " << y00 * h_spherical_integrals_(i1, i1) << std::endl
                              << " h(2,2) = " << y00 * h_spherical_integrals_(i2, i2) << std::endl 
                              << " surf_{12} = " << surf12 << std::endl
                              << " surf_{21} = " << surf21;
                            
                            warning(__FILE__, __LINE__, s, 0);
                        }

                        if (true)
                        {
                            double d = (v1 - v2);
                            h_spherical_integrals_(i1, i2) -= 0.5 * d / y00;
                            h_spherical_integrals_(i2, i1) += 0.5 * d / y00;

                        }
                    }
                }
            }
                
            o_radial_integrals_.zero();
            for (int l = 0; l <= atom_type_->indexr().lmax(); l++)
            {
                int nrf = atom_type_->indexr().num_rf(l);

                for (int order1 = 0; order1 < nrf; order1++)
                {
                    int idxrf1 = atom_type_->indexr().index_by_l_order(l, order1);
                    for (int order2 = 0; order2 < nrf; order2++)
                    {
                        int idxrf2 = atom_type_->indexr().index_by_l_order(l, order2);

                        if (order1 == order2) 
                        {
                            o_radial_integrals_(l, order1, order2) = 1.0;
                        }
                        else
                        {
                            for (int ir = 0; ir < nmtp; ir++)
                                s[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0);
                            o_radial_integrals_(l, order1, order2) = s.interpolate().integrate(2);
                        }
                    }
                }
            }

            if (true) // TODO: if it's slow, compute only when spin-orbit is turned on
            {
                double soc = pow(2 * speed_of_light, -2);

                Spline<double> s1(nmtp, atom_type_->radial_grid()); 
                
                Spline<double> ve(nmtp, atom_type_->radial_grid()); 
                for (int i = 0; i < nmtp; i++)
                    ve[i] = spherical_potential_[i] + atom_type_->zn() / atom_type_->radial_grid(i);
                ve.interpolate();

                so_radial_integrals_.zero();
                for (int l = 0; l <= atom_type_->indexr().lmax(); l++)
                {
                    int nrf = atom_type_->indexr().num_rf(l);
    
                    for (int order1 = 0; order1 < nrf; order1++)
                    {
                        int idxrf1 = atom_type_->indexr().index_by_l_order(l, order1);
                        for (int order2 = 0; order2 < nrf; order2++)
                        {
                            int idxrf2 = atom_type_->indexr().index_by_l_order(l, order2);

                            for (int ir = 0; ir < nmtp; ir++)
                            {
                                double M = 1.0 - 2 * soc * spherical_potential_[ir];
                                // first part <f| dVe / dr |f'>
                                s[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0) * 
                                        soc * ve.deriv(1, ir) / pow(M, 2);

                                // second part <f| d(z/r) / dr |f'>
                                s1[ir] = radial_functions_(ir, idxrf1, 0) * radial_functions_(ir, idxrf2, 0) *
                                         soc * atom_type_->zn() / pow(M, 2);
                            }
                            s.interpolate();
                            s1.interpolate();

                            so_radial_integrals_(l, order1, order2) = s.integrate(1) + s1.integrate(-1);
                        }
                    }
                }
            }
        }

        inline void sync_radial_integrals(int rank)
        {
            Platform::bcast(h_spherical_integrals_.get_ptr(), (int)h_spherical_integrals_.size(), rank);
            Platform::bcast(o_radial_integrals_.get_ptr(), (int)o_radial_integrals_.size(), rank);
            Platform::bcast(so_radial_integrals_.get_ptr(), (int)so_radial_integrals_.size(), rank);
        }

        inline double radial_function(int ir, int idx)
        {
            return radial_functions_(ir, idx, 0);
        }

        inline double h_radial_function(int ir, int idx)
        {
            return radial_functions_(ir, idx, 1);
        }
        
        inline double h_spherical_integral(int i1, int i2)
        {
            return h_spherical_integrals_(i1, i2);
        }

        inline double o_radial_integral(int l, int order1, int order2)
        {
            return o_radial_integrals_(l, order1, order2);
        }
        
        inline double so_radial_integral(int l, int order1, int order2)
        {
            return so_radial_integrals_(l, order1, order2);
        }

        void write_enu(char* buf, size_t n)
        {
            int j;
            j = snprintf(buf, n, "Atom : %s, class id : %i\n", atom_type_->label().c_str(), id_); 
            j += snprintf(buf + j, n, "augmented waves\n");
            for (int l = 0; l < num_aw_descriptors(); l++)
            {
                for (int order = 0; order < (int)aw_descriptor(l).size(); order++)
                {
                    radial_solution_descriptor& rsd = aw_descriptor(l)[order];
                    j += snprintf(buf + j, n, "n = %2i   l = %2i   order = %i   enu = %12.6f\n", 
                                  rsd.n, rsd.l, order, rsd.enu);
                }
            }

            j += snprintf(buf + j, n, "local orbitals\n");
            for (int idxlo = 0; idxlo < num_lo_descriptors(); idxlo++)
            {
                for (int order = 0; order < (int)lo_descriptor(idxlo).size(); order++)
                {
                    radial_solution_descriptor& rsd = lo_descriptor(idxlo)[order];
                    j +=snprintf(buf + j, n, "n = %2i   l = %2i   order = %i   enu = %12.6f\n", 
                                 rsd.n, rsd.l, order, rsd.enu);
                }
            }
            j += snprintf(buf + j, n, "\n");
        }
        
        /// Compute m-th order radial derivative at the MT surface
        double aw_surface_dm(int l, int order, int dm)
        {
            switch (dm)
            {
                case 0:
                {
                    int idxrf = atom_type_->indexr().index_by_l_order(l, order);
                    return radial_functions_(atom_type_->num_mt_points() - 1, idxrf, 0);
                }
                case 1:
                {
                    return aw_surface_derivatives_(order, l);
                }
                default:
                {
                    error(__FILE__, __LINE__, "wrong order of radial derivative", fatal_err);
                }
            }

            return 0.0; // just to make compiler happy
        }

        void generate_core_charge_density()
        {
            Timer t("sirius::AtomSymmetryClass::generate_core_charge_density");

            if (atom_type_->num_core_electrons() == 0.0) return;
            
            RadialSolver solver(true, -1.0 * atom_type_->zn(), atom_type_->radial_grid());
            
            Spline<double> rho(atom_type_->radial_grid().size(), atom_type_->radial_grid());
            
            std::vector<double> level_energy(atom_type_->num_atomic_levels());
    
            for (int ist = 0; ist < atom_type_->num_atomic_levels(); ist++)
                level_energy[ist] = -1.0 * atom_type_->zn() / 2 / pow(double(atom_type_->atomic_level(ist).n), 2);
            
            memset(&rho[0], 0, rho.num_points() * sizeof(double));
            #pragma omp parallel default(shared)
            {
                std::vector<double> rho_t(rho.num_points());
                memset(&rho_t[0], 0, rho.num_points() * sizeof(double));
                std::vector<double> p;
                
                #pragma omp for
                for (int ist = 0; ist < atom_type_->num_atomic_levels(); ist++)
                {
                    if (atom_type_->atomic_level(ist).core)
                    {
                        solver.bound_state(atom_type_->atomic_level(ist).n, atom_type_->atomic_level(ist).l, 
                                           spherical_potential_, level_energy[ist], p);
                
                        for (int i = 0; i < atom_type_->radial_grid().size(); i++)
                        {
                            rho_t[i] += atom_type_->atomic_level(ist).occupancy * 
                                        pow(y00 * p[i] / atom_type_->radial_grid(i), 2);
                        }
                    }
                }

                #pragma omp critical
                for (int i = 0; i < rho.num_points(); i++) rho[i] += rho_t[i];
            } 
                
            core_charge_density_ = rho.data_points();
            rho.interpolate();

            Spline<double> rho_mt(atom_type_->num_mt_points(), atom_type_->radial_grid());
            rho_mt.interpolate(core_charge_density_);

            core_leakage_ = fourpi * (rho.integrate(2) - rho_mt.integrate(2));

            core_eval_sum_ = 0.0;
            for (int ist = 0; ist < atom_type_->num_atomic_levels(); ist++)
            {
                if (atom_type_->atomic_level(ist).core)
                {
                    assert(level_energy[ist] == level_energy[ist]);
                    core_eval_sum_ += level_energy[ist] * atom_type_->atomic_level(ist).occupancy;
                }
            }
            assert(core_eval_sum_ == core_eval_sum_);
        }

        inline void sync_core_charge_density(int rank)
        {
            assert(core_charge_density_.size() != 0);
            assert(&core_charge_density_[0] != NULL);

            Platform::bcast(&core_charge_density_[0],  atom_type_->radial_grid().size(), rank);
            Platform::bcast(&core_leakage_, 1, rank);
            Platform::bcast(&core_eval_sum_, 1, rank);
        }

        double core_charge_density(int ir)
        {
            assert(ir >= 0 && ir < (int)core_charge_density_.size());

            return core_charge_density_[ir];
        }

        inline AtomType* atom_type()
        {
            return atom_type_;
        }

        inline double core_eval_sum()
        {
            return core_eval_sum_;
        }

        inline double core_leakage()
        {
            return core_leakage_;
        }
        
        inline int num_aw_descriptors()
        {
            return (int)aw_descriptors_.size();
        }

        inline radial_solution_descriptor_set& aw_descriptor(int idx)
        {
            return aw_descriptors_[idx];
        }
        
        inline int num_lo_descriptors()
        {
            return (int)lo_descriptors_.size();
        }

        inline radial_solution_descriptor_set& lo_descriptor(int idx)
        {
            return lo_descriptors_[idx];
        }
};

};

#endif // __ATOM_SYMMETRY_CLASS_H__
