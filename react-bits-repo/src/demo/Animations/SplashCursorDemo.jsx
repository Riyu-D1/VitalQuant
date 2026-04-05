import { useMemo } from 'react';
import { Flex, Text } from '@chakra-ui/react';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import CodeExample from '../../components/code/CodeExample';
import PropTable from '../../components/common/Preview/PropTable';
import useComponentProps from '../../hooks/useComponentProps';

import OpenInStudioButton from '../../components/common/Preview/OpenInStudioButton';

import SplashCursor from '../../content/Animations/SplashCursor/SplashCursor';
import { splashCursor } from '../../constants/code/Animations/splashCursorCode';

const DEFAULT_PROPS = {};

const SplashCursorDemo = () => {
  const { props, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);

  const propData = useMemo(
    () => [
      {
        name: 'SIM_RESOLUTION',
        type: 'number',
        default: '128',
        description: 'Fluid simulation resolution for velocity fields.'
      },
      {
        name: 'DYE_RESOLUTION',
        type: 'number',
        default: '1440',
        description: 'Resolution of the color/dye texture.'
      },
      {
        name: 'CAPTURE_RESOLUTION',
        type: 'number',
        default: '512',
        description: 'Resolution used for certain capture operations (rarely changed).'
      },
      {
        name: 'DENSITY_DISSIPATION',
        type: 'number',
        default: '3.5',
        description: 'Rate at which color/density dissipates over time.'
      },
      {
        name: 'VELOCITY_DISSIPATION',
        type: 'number',
        default: '2',
        description: 'Rate at which velocity dissipates over time.'
      },
      {
        name: 'PRESSURE',
        type: 'number',
        default: '0.1',
        description: 'Base pressure for the fluid simulation.'
      },
      {
        name: 'PRESSURE_ITERATIONS',
        type: 'number',
        default: '20',
        description: 'Number of Jacobi iterations used for the pressure solver.'
      },
      {
        name: 'CURL',
        type: 'number',
        default: '3',
        description: 'Amount of vorticity/curl to apply for swirling effects.'
      },
      {
        name: 'SPLAT_RADIUS',
        type: 'number',
        default: '0.2',
        description: "Radius of the 'splat' effect when user interacts."
      },
      {
        name: 'SPLAT_FORCE',
        type: 'number',
        default: '6000',
        description: "Force of the fluid 'splat' on each interaction."
      },
      {
        name: 'SHADING',
        type: 'boolean',
        default: 'true',
        description: 'Toggles simple lighting/shading on the fluid.'
      },
      {
        name: 'COLOR_UPDATE_SPEED',
        type: 'number',
        default: '10',
        description: 'Frequency at which pointer colors are re-randomized.'
      },
      {
        name: 'BACK_COLOR',
        type: 'object',
        default: '{ r: 0.5, g: 0, b: 0 }',
        description: 'Base background color for the fluid. Not always used if TRANSPARENT is true.'
      },
      {
        name: 'TRANSPARENT',
        type: 'boolean',
        default: 'true',
        description: 'Determines if the canvas background should be rendered with alpha.'
      }
    ],
    []
  );

  console.log(propData);

  return (
    <>
      <ComponentPropsProvider
        props={props}
        defaultProps={DEFAULT_PROPS}
        resetProps={resetProps}
        hasChanges={hasChanges}
      >
        <TabsLayout>
          <PreviewTab>
            <Flex
              overflow="hidden"
              justifyContent="center"
              flexDirection={'column'}
              minH={300}
              p={0}
              alignItems="center"
              className="demo-container"
              position={'relative'}
              zIndex={10}
            >
              <Text fontSize={'3rem'} textAlign="center" color="#271E37" fontWeight={900} userSelect={'none'}>
                Move Your Cursor
              </Text>
            </Flex>

            <Flex justify="flex-end" mt={2} mb={-2}>
              <OpenInStudioButton backgroundId="splash-cursor" currentProps={{}} defaultProps={DEFAULT_PROPS} />
            </Flex>

            <PropTable data={propData} />
          </PreviewTab>

          <CodeTab>
            <CodeExample codeObject={splashCursor} componentName="SplashCursor" />
          </CodeTab>
        </TabsLayout>
      </ComponentPropsProvider>

      <SplashCursor />
    </>
  );
};

export default SplashCursorDemo;
