import { useMemo } from 'react';
import { Box, Icon } from '@chakra-ui/react';
import { FaPlusCircle, FaMinusCircle } from 'react-icons/fa';
import { CodeTab, PreviewTab, TabsLayout } from '../../components/common/TabsLayout';

import useComponentProps from '../../hooks/useComponentProps';
import { ComponentPropsProvider } from '../../components/context/ComponentPropsContext';

import PropTable from '../../components/common/Preview/PropTable';
import Dependencies from '../../components/code/Dependencies';
import CodeExample from '../../components/code/CodeExample';

import ElasticSlider from '../../content/Components/ElasticSlider/ElasticSlider';
import { elasticSlider } from '../../constants/code/Components/elasticSliderCode';

const DEFAULT_PROPS = {
  defaultValue: 50,
  startingValue: 0,
  maxValue: 100,
  isStepped: false,
  stepSize: 1
};

const ElasticSliderDemo = () => {
  const { props, resetProps, hasChanges } = useComponentProps(DEFAULT_PROPS);
  const { defaultValue, startingValue, maxValue, isStepped, stepSize } = props;

  const propData = useMemo(
    () => [
      {
        name: 'defaultValue',
        type: 'number',
        default: '50',
        description: 'The initial value of the slider. It can be less than startingValue or greater than maxValue.'
      },
      {
        name: 'startingValue',
        type: 'number',
        default: '0',
        description:
          "The starting point for the slider's range, e.g., startingValue=100 allows the slider to start at 100."
      },
      {
        name: 'maxValue',
        type: 'number',
        default: '100',
        description: 'The maximum value the slider can reach.'
      },
      {
        name: 'className',
        type: 'string',
        default: '',
        description: 'Allows passing custom class names to style the component.'
      },
      {
        name: 'isStepped',
        type: 'boolean',
        default: 'false',
        description: 'Enables or disables stepped increments on the slider.'
      },
      {
        name: 'stepSize',
        type: 'number',
        default: '1',
        description: 'The size of the increments for the slider when isStepped is enabled.'
      },
      {
        name: 'leftIcon',
        type: 'JSX.Element',
        default: '<>-</>',
        description: 'Custom JSX or HTML code to display on the left side of the slider.'
      },
      {
        name: 'rightIcon',
        type: 'JSX.Element',
        default: '<>+</>',
        description: 'Custom JSX or HTML code to display on the right side of the slider.'
      }
    ],
    []
  );

  return (
    <ComponentPropsProvider props={props} defaultProps={DEFAULT_PROPS} resetProps={resetProps} hasChanges={hasChanges}>
      <TabsLayout>
        <PreviewTab>
          <h2 className="demo-title-extra">Default</h2>
          <Box position="relative" className="demo-container" minH={200}>
            <ElasticSlider
              defaultValue={defaultValue}
              startingValue={startingValue}
              maxValue={maxValue}
              isStepped={isStepped}
              stepSize={stepSize}
            />
          </Box>

          <h2 className="demo-title-extra">Steps</h2>
          <Box position="relative" className="demo-container" minH={200}>
            <ElasticSlider isStepped stepSize={10} />
          </Box>

          <h2 className="demo-title-extra">Custom Values & Icons</h2>
          <Box position="relative" className="demo-container" minH={200}>
            <ElasticSlider
              leftIcon={<Icon as={FaMinusCircle} />}
              rightIcon={<Icon as={FaPlusCircle} />}
              startingValue={500}
              defaultValue={750}
              maxValue={1000}
            />
          </Box>

          <PropTable data={propData} />
          <Dependencies dependencyList={['motion']} />
        </PreviewTab>

        <CodeTab>
          <CodeExample codeObject={elasticSlider} componentName="ElasticSlider" />
        </CodeTab>
      </TabsLayout>
    </ComponentPropsProvider>
  );
};

export default ElasticSliderDemo;
