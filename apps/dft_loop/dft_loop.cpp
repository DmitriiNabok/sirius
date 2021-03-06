#include <sirius.h>

int main(int argn, char** argv)
{
    Platform::initialize(1);
    
    sirius::Global global;

    double a1[] = {2.708, 2.708, -2.708};
    double a2[] = {2.708, -2.708, 2.708};
    double a3[] = {-2.708, 2.708, 2.708};
                
    global.set_lattice_vectors(a1, a2, a3);
    //global.set_num_spins(2);
    //global.set_num_mag_dims(1);

    double pos[] = {0, 0, 0};
    double vf[] = {0, 0, 1};
    global.add_atom_type(0, "Fe");
    global.add_atom(0, pos, vf);

    global.initialize();
    
    global.print_info();
    
    sirius::Potential* potential = 
        new sirius::Potential(global, sirius::rlm_component | sirius::it_component | sirius::pw_component);
        
    
    int ngridk[] = {1, 1, 1};
    int numkp = ngridk[0] * ngridk[1] * ngridk[2];
    int ik = 0;
    mdarray<double, 2> kpoints(3, numkp);
    std::vector<double> kpoint_weights(numkp);

    for (int i0 = 0; i0 < ngridk[0]; i0++) 
        for (int i1 = 0; i1 < ngridk[1]; i1++) 
            for (int i2 = 0; i2 < ngridk[2]; i2++)
            {
                kpoints(0, ik) = double(i0) / ngridk[0];
                kpoints(1, ik) = double(i1) / ngridk[1];
                kpoints(2, ik) = double(i2) / ngridk[2];
                kpoint_weights[ik] = 1.0 / numkp;
                ik++;
            }

    sirius::Density* density = 
        new sirius::Density(global, potential, kpoints, &kpoint_weights[0], 
                            sirius::rlm_component | sirius::it_component | sirius::pw_component);

    density->print_info();
    
    density->initial_density();
    
    potential->generate_effective_potential(density->rho(), density->magnetization());

    density->find_eigen_states();
    density->find_band_occupancies();
    density->generate();
    
    potential->generate_effective_potential(density->rho(), density->magnetization());

    delete density;
    delete potential;
    
    global.clear();

    sirius::Timer::print();
}
